/*
 * Cache controller class for ffmpegfs
 *
 * Copyright (c) 2017 by Norbert Schlia (nschlia@oblivion-software.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "cache.h"
#include "cache_entry.h"

#include "transcode.h"
#include "ffmpeg_utils.h"

#include <sys/statvfs.h>
#include <assert.h>
#include <vector>

#ifndef HAVE_SQLITE_ERRSTR
#define sqlite3_errstr(rc)  ""
#endif // HAVE_SQLITE_ERRSTR

using namespace std;

Cache::Cache() :
    m_mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP),
    m_cacheidx_db(NULL),
    m_cacheidx_select_stmt(NULL),
    m_cacheidx_insert_stmt(NULL),
    m_cacheidx_delete_stmt(NULL)
{
}

Cache::~Cache()
{

    close_index();

    // Clean up memory
    for (cache_t::iterator p = m_cache.begin(); p != m_cache.end(); ++p)
    {
        delete (Cache_Entry *)p->second;
    }

    m_cache.clear();
}

static int callback(void * /*NotUsed*/, int /*argc*/, char ** /*argv*/, char ** /*azColName*/)
{
    return 0;
}

bool Cache::load_index()
{
    bool success = true;

    try
    {
        char cachepath[PATH_MAX];
        char *errmsg = NULL;
        const char * sql;
        int ret;

        cache_path(cachepath, sizeof(cachepath));

        m_cacheidx_file = cachepath;
        m_cacheidx_file += "/";
        m_cacheidx_file += "cacheidx.sqlite";

        if (mktree(cachepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) && errno != EEXIST)
        {
            ffmpegfs_error("Error creating cache directory '%s': %s", cachepath, strerror(errno));
            throw false;
        }

        // initialise engine
        if (SQLITE_OK != (ret = sqlite3_initialize()))
        {
            ffmpegfs_error("Failed to initialise SQLite3 library: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
        // open connection to a DB
        if (SQLITE_OK != (ret = sqlite3_open_v2(m_cacheidx_file.c_str(), &m_cacheidx_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)))
        {
            ffmpegfs_error("Failed to initialise SQLite3 connection: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        // Create table and index if nit existing
        sql =   "CREATE TABLE IF NOT EXISTS cache_entry\n"
                "(\n"
                "id                 INTEGER PRIMARY KEY ASC,\n"
                "filename           TEXT UNIQUE NOT NULL,\n"
                "target_format      CHAR(10) NOT NULL,\n"
                "predicted_filesize UNSIGNED BIG INT NOT NULL,\n"
                "encoded_filesize   UNSIGNED BIG INT NOT NULL,\n"
                "finished           BOOLEAN NOT NULL,\n"
                "error              BOOLEAN NOT NULL,\n"
                "creation_time      DATETIME NOT NULL,\n"
                "access_time        DATETIME NOT NULL,\n"
                "file_time          DATETIME NOT NULL,\n"
                "file_size         UNSIGNED BIG INT NOT NULL\n"
                ");\n"
                "CREATE UNIQUE INDEX IF NOT EXISTS \"filename_idx\" ON \"cache_entry\" (\"filename\" ASC);\n";

        if (SQLITE_OK != (ret = sqlite3_exec(m_cacheidx_db, sql, callback, 0, &errmsg)))
        {
            ffmpegfs_error("SQLite3 exec error: %d, %s", ret, errmsg);
            throw false;
        }

#ifdef HAVE_SQLITE_CACHEFLUSH
        if (!flush_index())
        {
            throw false;
        }
#endif // HAVE_SQLITE_CACHEFLUSH

        // prepare the statements

        sql =   "INSERT OR REPLACE INTO cache_entry\n"
                "(filename, target_format, predicted_filesize, encoded_filesize, finished, error, creation_time, access_time, file_time, file_size) VALUES\n"
                "(?, ?, ?, ?, ?, ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?);\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_insert_stmt, NULL)))
        {
            ffmpegfs_error("Failed to prepare insert: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "SELECT target_format, predicted_filesize, encoded_filesize, finished, error, strftime('%s', creation_time), strftime('%s', access_time), strftime('%s', file_time), file_size FROM cache_entry WHERE filename = ?;\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_select_stmt, NULL)))
        {
            ffmpegfs_error("Failed to prepare select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "DELETE FROM cache_entry WHERE filename = ?;\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_delete_stmt, NULL)))
        {
            ffmpegfs_error("Failed to prepare delete: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }
    }
    catch (bool _success)
    {
        success = _success;
    }

    return success;
}

#ifdef HAVE_SQLITE_CACHEFLUSH
bool Cache::flush_index()
{
    if (m_cacheidx_db != NULL)
    {
        int ret;

        // Flush cache to disk
        if (SQLITE_OK != (ret = sqlite3_db_cacheflush(m_cacheidx_db)))
        {
            ffmpegfs_error("SQLite3 cache flush error: %d, %s", ret, sqlite3_errstr(ret));
            return false;
        }
    }
    return true;
}
#endif // HAVE_SQLITE_CACHEFLUSH

bool Cache::read_info(t_cache_info & cache_info)
{
    int ret;
    bool success = true;

    if (m_cacheidx_select_stmt == NULL)
    {
        ffmpegfs_error("SQLite3 select statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_select_stmt) == 1);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_select_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_select_stmt);

        if (ret == SQLITE_ROW)
        {
            const char *text                = (const char *)sqlite3_column_text(m_cacheidx_select_stmt, 0);
            if (text != NULL)
            {
                cache_info.m_target_format[0] = '\0';
                strncat(cache_info.m_target_format, text, sizeof(cache_info.m_target_format) - 1);
            }
            cache_info.m_predicted_filesize = sqlite3_column_int64(m_cacheidx_select_stmt, 1);
            cache_info.m_encoded_filesize   = sqlite3_column_int64(m_cacheidx_select_stmt, 2);
            cache_info.m_finished           = sqlite3_column_int(m_cacheidx_select_stmt, 3);
            cache_info.m_error              = sqlite3_column_int(m_cacheidx_select_stmt, 4);
            cache_info.m_creation_time      = sqlite3_column_int64(m_cacheidx_select_stmt, 5);
            cache_info.m_access_time        = sqlite3_column_int64(m_cacheidx_select_stmt, 6);
            cache_info.m_file_time          = sqlite3_column_int64(m_cacheidx_select_stmt, 7);
            cache_info.m_file_size          = sqlite3_column_int64(m_cacheidx_select_stmt, 8);
        }
        else if (ret != SQLITE_DONE)
        {
            ffmpegfs_error("Sqlite 3 could not step (execute) insert stmt: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
    }
    catch (bool _success)
    {
        success = _success;
    }

    sqlite3_reset(m_cacheidx_select_stmt);

    unlock();

    if (success)
    {
        errno = 0; // sqlite3 sometimes sets errno without any reason, better reset any error
    }

    return success;
}

bool Cache::write_info(const t_cache_info & cache_info)
{
    int ret;
    bool success = true;

    if (m_cacheidx_insert_stmt == NULL)
    {
        ffmpegfs_error("SQLite3 select statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_insert_stmt) == 10);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_insert_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_insert_stmt, 2, cache_info.m_target_format, -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 3, cache_info.m_predicted_filesize)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 4, cache_info.m_encoded_filesize)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int(m_cacheidx_insert_stmt, 5, cache_info.m_finished)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int(m_cacheidx_insert_stmt, 6, cache_info.m_error)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 7, cache_info.m_creation_time)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 8, cache_info.m_access_time)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 9, cache_info.m_file_time)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 10, cache_info.m_file_size)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_insert_stmt);

        if (ret != SQLITE_DONE)
        {
            ffmpegfs_error("Sqlite 3 could not step (execute) select statement: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
    }
    catch (bool _success)
    {
        success = _success;
    }

    sqlite3_reset(m_cacheidx_insert_stmt);

    unlock();

    if (success)
    {
        errno = 0; // sqlite3 sometimes sets errno without any reason, better reset any error
    }

    return success;
}

bool Cache::delete_info(const string & filename)
{
    int ret;
    bool success = true;

    if (m_cacheidx_delete_stmt == NULL)
    {
        ffmpegfs_error("SQLite3 delete statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_delete_stmt) == 1);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_delete_stmt, 1, filename.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_delete_stmt);

        if (ret != SQLITE_DONE)
        {
            ffmpegfs_error("Sqlite 3 could not step (execute) delete statement: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
    }
    catch (bool _success)
    {
        success = _success;
    }

    sqlite3_reset(m_cacheidx_delete_stmt);

    unlock();

    if (success)
    {
        errno = 0; // sqlite3 sometimes sets errno without any reason, better reset any error
    }

    return success;
}

void Cache::close_index()
{
    if (m_cacheidx_db != NULL)
    {
#ifdef HAVE_SQLITE_CACHEFLUSH
        flush_index();
#endif // HAVE_SQLITE_CACHEFLUSH

        sqlite3_finalize(m_cacheidx_select_stmt);
        sqlite3_finalize(m_cacheidx_insert_stmt);
        sqlite3_finalize(m_cacheidx_delete_stmt);

        sqlite3_close(m_cacheidx_db);
    }
    sqlite3_shutdown();
}

Cache_Entry* Cache::create_entry(const string & filename)
{
    Cache_Entry* cache_entry = new Cache_Entry(this, filename);
    if (cache_entry == NULL)
    {
        ffmpegfs_error("Out of memory for '%s'.", filename.c_str());
        return NULL;
    }
    m_cache.insert(make_pair(filename, cache_entry));
    return cache_entry;
}

bool Cache::delete_entry(Cache_Entry ** cache_entry, int flags)
{
    if (*cache_entry == NULL)
    {
        return true;
    }

    if ((*cache_entry)->close(flags))
    {
        // If CLOSE_CACHE_FREE is set, also free memory
        if (CACHE_CHECK_BIT(CLOSE_CACHE_FREE, flags))
        {
            m_cache.erase((*cache_entry)->m_cache_info.m_filename);

            delete (*cache_entry);
            *cache_entry = NULL;

            return true; // Freed entry
        }
    }

    return false;   // Kept entry
}

Cache_Entry* Cache::open(const char *filename)
{
    Cache_Entry* cache_entry = NULL;
    char resolved_name[PATH_MAX];
    string sanitised_name;

    if (realpath(filename, resolved_name) == NULL)
    {
        sanitised_name = filename;
    }
    else
    {
        sanitised_name = resolved_name;
    }

    cache_t::iterator p = m_cache.find(sanitised_name);
    if (p == m_cache.end())
    {
        ffmpegfs_trace("Created new transcoder for '%s'.", sanitised_name.c_str());
        cache_entry = create_entry(sanitised_name);
    }
    else
    {
        ffmpegfs_trace("Reusing cached transcoder for '%s'.", sanitised_name.c_str());
        cache_entry = p->second;
    }

    return cache_entry;
}

bool Cache::close(Cache_Entry **cache_entry, int flags /*= CLOSE_CACHE_DELETE*/)
{
    if ((*cache_entry) == NULL)
    {
        return true;
    }

    string filename((*cache_entry)->filename());
    if (delete_entry(cache_entry, flags))
    {
        ffmpegfs_trace("Freed cache entry for '%s'.", filename.c_str());
        return true;
    }
    else
    {
        ffmpegfs_trace("Keeping cache entry for '%s'.", filename.c_str());
        return false;
    }
}

bool Cache::prune_expired()
{
    if (params.m_expiry_time <= 0)
    {
        // There's no limit.
        return true;
    }

    vector<string> filenames;
    sqlite3_stmt * stmt;
    time_t now = time(NULL);
    char sql[1024];
    char buffer[100];

    format_time(buffer, sizeof(buffer), params.m_expiry_time);

    fprintf(stderr, "Pruning expired cache entries older than %s...\n", buffer); fflush(stderr);

    sprintf(sql, "SELECT filename, strftime('%%s', access_time) FROM cache_entry WHERE strftime('%%s', access_time) + %zu < %zu;\n", params.m_expiry_time, now);

    sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

    int ret = 0;
    while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *filename = (const char *) sqlite3_column_text(stmt, 0);
        filenames.push_back(filename);

        format_time(buffer, sizeof(buffer), now - (time_t)sqlite3_column_int64(stmt, 1));
        fprintf(stderr, "Found %s old entry: %s\n", buffer, filename); fflush(stderr);
    }

    fprintf(stderr, "%zu expired cache entries found.\n", filenames.size()); fflush(stderr);

    if (ret == SQLITE_DONE)
    {
        for (vector<string>::const_iterator it = filenames.begin(); it != filenames.end(); it++)
        {
            const string & filename = *it;
            fprintf(stderr, "Pruning: %s\n", filename.c_str()); fflush(stderr);

            cache_t::iterator p = m_cache.find(filename);
            if (p != m_cache.end())
            {
                delete_entry(&p->second, CLOSE_CACHE_DELETE);
            }

            delete_info(filename);
        }
    }
    else
    {
        ffmpegfs_error("Failed to execute select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
    }

    sqlite3_finalize(stmt);

    return true;
}

bool Cache::prune_cache_size()
{
    if (!params.m_max_cache_size)
    {
        // There's no limit.
        return true;
    }

    vector<string> filenames;
    vector<size_t> filesizes;
    sqlite3_stmt * stmt;
    const char * sql;
    char buffer[100];

    format_size(buffer, sizeof(buffer), params.m_max_cache_size);

    fprintf(stderr, "Pruning oldest cache entries exceeding %s cache size...\n", buffer); fflush(stderr);

    sql = "SELECT filename, encoded_filesize FROM cache_entry ORDER BY access_time ASC;\n";

    sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

    int ret = 0;
    size_t total_size = 0;
    while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *filename = (const char *) sqlite3_column_text(stmt, 0);
        size_t size = (size_t)sqlite3_column_int64(stmt, 1);
        filenames.push_back(filename);
        filesizes.push_back(size);
        total_size += size;
    }

    format_size(buffer, sizeof(buffer), total_size);

    fprintf(stderr, "%s in cache.\n", buffer); fflush(stderr);

    if (total_size > params.m_max_cache_size)
    {
        format_size(buffer, sizeof(buffer), total_size - params.m_max_cache_size);

        fprintf(stderr, "Pruning %s of oldest cache entries to limit cache size.\n", buffer); fflush(stderr);

        if (ret == SQLITE_DONE)
        {
            int n = 0;
            for (vector<string>::const_iterator it = filenames.begin(); it != filenames.end(); it++)
            {
                const string & filename = *it;
                fprintf(stderr, "Pruning: %s\n", filename.c_str()); fflush(stderr);

                cache_t::iterator p = m_cache.find(filename);
                if (p != m_cache.end())
                {
                    delete_entry(&p->second, CLOSE_CACHE_DELETE);
                }

                delete_info(filename);

                total_size -= filesizes[n++];

                if (total_size <= params.m_max_cache_size)
                {
                    break;
                }
            }

            format_size(buffer, sizeof(buffer), total_size);

            fprintf(stderr, "%s left in cache.\n", buffer); fflush(stderr);
        }
        else
        {
            ffmpegfs_error("Failed to execute select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
        }
    }

    sqlite3_finalize(stmt);

    return true;
}

bool Cache::prune_disk_space(size_t predicted_filesize)
{
    char cachepath[PATH_MAX];
    struct statvfs buf;

    cache_path(cachepath, sizeof(cachepath));

    if (statvfs(cachepath, &buf))
    {
        fprintf(stderr, "prune_disk_space() cannot determine free disk space: %s\n", strerror(errno)); fflush(stderr);
        return false;
    }

    size_t free_bytes = buf.f_bfree * buf.f_bsize;
    char buffer[100];

    format_size(buffer, sizeof(buffer), free_bytes);

    fprintf(stderr, "%s disk space before prune.\n", buffer); fflush(stderr);
    if (free_bytes < params.m_min_diskspace + predicted_filesize)
    {
        vector<string> filenames;
        vector<size_t> filesizes;
        sqlite3_stmt * stmt;
        const char * sql;

        sql = "SELECT filename, encoded_filesize FROM cache_entry ORDER BY access_time ASC;\n";

        sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

        int ret = 0;
        while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            const char *filename = (const char *) sqlite3_column_text(stmt, 0);
            size_t size = (size_t)sqlite3_column_int64(stmt, 1);
            filenames.push_back(filename);
            filesizes.push_back(size);
        }

        char prunesize[100];
        char diskspace[100];

        format_size(prunesize, sizeof(prunesize), params.m_min_diskspace + predicted_filesize - free_bytes);
        format_size(diskspace, sizeof(diskspace), params.m_min_diskspace);

        fprintf(stderr, "Pruning %s of oldest cache entries to keep disk space above %s limit...\n", prunesize, diskspace); fflush(stderr);

        if (ret == SQLITE_DONE)
        {
            int n = 0;
            for (vector<string>::const_iterator it = filenames.begin(); it != filenames.end(); it++)
            {
                const string & filename = *it;
                fprintf(stderr, "Pruning: %s\n", filename.c_str()); fflush(stderr);

                cache_t::iterator p = m_cache.find(filename);
                if (p != m_cache.end())
                {
                    delete_entry(&p->second, CLOSE_CACHE_DELETE);
                }

                delete_info(filename);

                free_bytes += filesizes[n++];

                if (free_bytes >= params.m_min_diskspace + predicted_filesize)
                {
                    break;
                }
            }

            format_size(buffer, sizeof(buffer), free_bytes);

            fprintf(stderr, "Disk space after prune: %s\n", buffer); fflush(stderr);
        }
        else
        {
            ffmpegfs_error("Failed to execute select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
        }

        sqlite3_finalize(stmt);
    }

    return true;
}

bool Cache::prune_cache(size_t predicted_filesize)
{
    bool bSuccess = true;

    lock();

    // Find and remove expired cache entries
    bSuccess &= prune_expired();

    // Check max. cache size
    bSuccess &= prune_cache_size();

    // Check min. diskspace required for cache
    bSuccess &= prune_disk_space(predicted_filesize);

    unlock();

    return bSuccess;
}

void Cache::lock()
{
    pthread_mutex_lock(&m_mutex);
}

void Cache::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}
