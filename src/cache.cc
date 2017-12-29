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
    // Clean up memory
    for (cache_t::iterator p = m_cache.begin(); p != m_cache.end(); ++p)
    {
        delete (Cache_Entry *)p->second;
    }

    m_cache.clear();

    close_index();
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
        if (SQLITE_OK != (ret = sqlite3_open_v2(m_cacheidx_file.c_str(), &m_cacheidx_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_SHAREDCACHE, NULL)))
        {
            ffmpegfs_error("Failed to initialise SQLite3 connection: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        // Create cache_entry table not already existing
        sql =
                "CREATE TABLE IF NOT EXISTS `cache_entry` (\n"
                //
                // Primary key: filename + desttype
                //
                "    `filename`             TEXT NOT NULL,\n"
                "    `desttype`             CHAR ( 10 ) NOT NULL,\n"
                //
                // Encoding parameters
                //
                "    `enable_ismv`          BOOLEAN NOT NULL,\n"
                "    `audiobitrate`         UNSIGNED INT NOT NULL,\n"
                "    `audiosamplerate`      UNSIGNED INT NOT NULL,\n"
                "    `videobitrate`         UNSIGNED INT NOT NULL,\n"
                "    `videowidth`           UNSIGNED INT NOT NULL,\n"
                "    `videoheight`          UNSIGNED INT NOT NULL,\n"
                "    `deinterlace`          BOOLEAN NOT NULL,\n"
                //
                // Encoding results
                //
                "    `predicted_filesize`	UNSIGNED BIG INT NOT NULL,\n"
                "    `encoded_filesize`     UNSIGNED BIG INT NOT NULL,\n"
                "    `finished`             BOOLEAN NOT NULL,\n"
                "    `error`                BOOLEAN NOT NULL,\n"
                "    `errno`                INT NOT NULL,\n"
                "    `averror`              INT NOT NULL,\n"
                "    `creation_time`        DATETIME NOT NULL,\n"
                "    `access_time`          DATETIME NOT NULL,\n"
                "    `file_time`            DATETIME NOT NULL,\n"
                "    `file_size`            UNSIGNED BIG INT NOT NULL,\n"
                "    PRIMARY KEY(`filename`,`desttype`)\n"
                ");\n";
                //"CREATE UNIQUE INDEX IF NOT EXISTS `idx_cache_entry_key` ON `cache_entry` (`filename`,`desttype`);\n";

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
                "(filename, desttype, enable_ismv, audiobitrate, audiosamplerate, videobitrate, videowidth, videoheight, deinterlace, predicted_filesize, encoded_filesize, finished, error, errno, averror, creation_time, access_time, file_time, file_size) VALUES\n"
                "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?);\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_insert_stmt, NULL)))
        {
            ffmpegfs_error("Failed to prepare insert: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "SELECT desttype, enable_ismv, audiobitrate, audiosamplerate, videobitrate, videowidth, videoheight, deinterlace, predicted_filesize, encoded_filesize, finished, error, errno, averror, strftime('%s', creation_time), strftime('%s', access_time), strftime('%s', file_time), file_size FROM cache_entry WHERE filename = ? AND desttype = ?;\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_select_stmt, NULL)))
        {
            ffmpegfs_error("Failed to prepare select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "DELETE FROM cache_entry WHERE filename = ? AND desttype = ?;\n";

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
        assert(sqlite3_bind_parameter_count(m_cacheidx_select_stmt) == 2);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_select_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error 'filename': %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_select_stmt, 2, cache_info.m_desttype, -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error 'desttype': %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_select_stmt);

        if (ret == SQLITE_ROW)
        {
            const char *text                = (const char *)sqlite3_column_text(m_cacheidx_select_stmt, 0);
            if (text != NULL)
            {
                cache_info.m_desttype[0] = '\0';
                strncat(cache_info.m_desttype, text, sizeof(cache_info.m_desttype) - 1);
            }
            
            cache_info.m_enable_ismv        = sqlite3_column_int(m_cacheidx_select_stmt, 1);
            cache_info.m_audiobitrate       = sqlite3_column_int(m_cacheidx_select_stmt, 2);
            cache_info.m_audiosamplerate    = sqlite3_column_int(m_cacheidx_select_stmt, 3);
            cache_info.m_videobitrate       = sqlite3_column_int(m_cacheidx_select_stmt, 4);
            cache_info.m_videowidth         = sqlite3_column_int(m_cacheidx_select_stmt, 5);
            cache_info.m_videoheight        = sqlite3_column_int(m_cacheidx_select_stmt, 6);
            cache_info.m_deinterlace        = sqlite3_column_int(m_cacheidx_select_stmt, 7);
            cache_info.m_predicted_filesize = sqlite3_column_int64(m_cacheidx_select_stmt, 8);
            cache_info.m_encoded_filesize   = sqlite3_column_int64(m_cacheidx_select_stmt, 9);
            cache_info.m_finished           = sqlite3_column_int(m_cacheidx_select_stmt, 10);
            cache_info.m_error              = sqlite3_column_int(m_cacheidx_select_stmt, 11);
            cache_info.m_errno              = sqlite3_column_int(m_cacheidx_select_stmt, 12);
            cache_info.m_averror            = sqlite3_column_int(m_cacheidx_select_stmt, 13);
            cache_info.m_creation_time      = sqlite3_column_int64(m_cacheidx_select_stmt, 14);
            cache_info.m_access_time        = sqlite3_column_int64(m_cacheidx_select_stmt, 15);
            cache_info.m_file_time          = sqlite3_column_int64(m_cacheidx_select_stmt, 16);
            cache_info.m_file_size          = sqlite3_column_int64(m_cacheidx_select_stmt, 17);
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

#define SQLBINDTXT(idx, var) \
    if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_insert_stmt, idx, var, -1, NULL))) \
{ \
    ffmpegfs_error("SQLite3 select column #%i error: %d, %s", idx, ret, sqlite3_errstr(ret)); \
    throw false; \
    }

#define SQLBINDNUM(func, idx, var) \
    if (SQLITE_OK != (ret = func(m_cacheidx_insert_stmt, idx, var))) \
{ \
    ffmpegfs_error("SQLite3 select column #%i error: %d, %s", idx, ret, sqlite3_errstr(ret)); \
    throw false; \
    }

bool Cache::write_info(const t_cache_info & cache_info)
{
    int ret;
    bool success = true;

    if (m_cacheidx_insert_stmt == NULL)
    {
        ffmpegfs_error("SQLite3 select statement not open.");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_insert_stmt) == 19);

        SQLBINDTXT(1, cache_info.m_filename.c_str());
        SQLBINDTXT(2, cache_info.m_desttype);
        SQLBINDNUM(sqlite3_bind_int, 3, cache_info.m_enable_ismv);
        SQLBINDNUM(sqlite3_bind_int, 4, cache_info.m_audiobitrate);
        SQLBINDNUM(sqlite3_bind_int, 5, cache_info.m_audiosamplerate);
        SQLBINDNUM(sqlite3_bind_int, 6, cache_info.m_videobitrate);
        SQLBINDNUM(sqlite3_bind_int, 7, cache_info.m_videowidth);
        SQLBINDNUM(sqlite3_bind_int, 8, cache_info.m_videoheight);
        SQLBINDNUM(sqlite3_bind_int, 9, cache_info.m_deinterlace);
        SQLBINDNUM(sqlite3_bind_int64, 10, cache_info.m_predicted_filesize);
        SQLBINDNUM(sqlite3_bind_int64, 11, cache_info.m_encoded_filesize);
        SQLBINDNUM(sqlite3_bind_int, 12, cache_info.m_finished);
        SQLBINDNUM(sqlite3_bind_int, 13, cache_info.m_error);
        SQLBINDNUM(sqlite3_bind_int, 14, cache_info.m_errno);
        SQLBINDNUM(sqlite3_bind_int, 15, cache_info.m_averror);
        SQLBINDNUM(sqlite3_bind_int64, 16, cache_info.m_creation_time);
        SQLBINDNUM(sqlite3_bind_int64, 17, cache_info.m_access_time);
        SQLBINDNUM(sqlite3_bind_int64, 18, cache_info.m_file_time);
        SQLBINDNUM(sqlite3_bind_int64, 19, cache_info.m_file_size);

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

bool Cache::delete_info(const string & filename, const string & desttype)
{
    int ret;
    bool success = true;

    if (m_cacheidx_delete_stmt == NULL)
    {
        ffmpegfs_error("SQLite3 delete statement not open.");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_delete_stmt) == 2);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_delete_stmt, 1, filename.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error 'filename': %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_delete_stmt, 2, desttype.c_str(), -1, NULL)))
        {
            ffmpegfs_error("SQLite3 select error 'desttype': %d, %s", ret, sqlite3_errstr(ret));
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

Cache_Entry* Cache::create_entry(const string & filename, const string & desttype)
{
    Cache_Entry* cache_entry = new Cache_Entry(this, filename);
    if (cache_entry == NULL)
    {
        ffmpegfs_error("Out of memory for '%s'.", filename.c_str());
        return NULL;
    }
    m_cache.insert(make_pair(make_pair(filename, desttype), cache_entry));
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
            m_cache.erase(make_pair((*cache_entry)->m_cache_info.m_filename, (*cache_entry)->m_cache_info.m_desttype));

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

    cache_t::iterator p = m_cache.find(make_pair(sanitised_name, params.m_desttype));
    if (p == m_cache.end())
    {
        ffmpegfs_trace("Created new transcoder for '%s'.", sanitised_name.c_str());
        cache_entry = create_entry(sanitised_name, params.m_desttype);
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

    vector<cache_key_t> keys;
    sqlite3_stmt * stmt;
    time_t now = time(NULL);
    char sql[1024];

    ffmpegfs_trace("Pruning expired cache entries older than %s...", format_time(params.m_expiry_time).c_str());

    sprintf(sql, "SELECT filename, desttype, strftime('%%s', access_time) FROM cache_entry WHERE strftime('%%s', access_time) + %zu < %zu;\n", params.m_expiry_time, now);

    sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

    int ret = 0;
    while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *filename = (const char *) sqlite3_column_text(stmt, 0);
        const char *desttype = (const char *) sqlite3_column_text(stmt, 1);

        keys.push_back(make_pair(filename, desttype));

        ffmpegfs_trace("Found %s old entry: %s", format_time(now - (time_t)sqlite3_column_int64(stmt, 2)).c_str(), filename);
    }

    ffmpegfs_trace("%zu expired cache entries found.", keys.size());

    if (ret == SQLITE_DONE)
    {
        for (vector<cache_key_t>::const_iterator it = keys.begin(); it != keys.end(); it++)
        {
            const cache_key_t & key = *it;
            ffmpegfs_trace("Pruning: %s Type: %s", key.first.c_str(), key.second.c_str());

            cache_t::iterator p = m_cache.find(key);
            if (p != m_cache.end())
            {
                delete_entry(&p->second, CLOSE_CACHE_DELETE);
            }

            delete_info(key.first, key.second);
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

    vector<cache_key_t> keys;
    vector<size_t> filesizes;
    sqlite3_stmt * stmt;
    const char * sql;

    ffmpegfs_trace("Pruning oldest cache entries exceeding %s cache size...", format_size(params.m_max_cache_size).c_str());

    sql = "SELECT filename, desttype, encoded_filesize FROM cache_entry ORDER BY access_time ASC;\n";

    sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

    int ret = 0;
    size_t total_size = 0;
    while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        const char *filename = (const char *) sqlite3_column_text(stmt, 0);
        const char *desttype = (const char *) sqlite3_column_text(stmt, 1);
        size_t size = (size_t)sqlite3_column_int64(stmt, 2);

        keys.push_back(make_pair(filename, desttype));
        filesizes.push_back(size);
        total_size += size;
    }

    ffmpegfs_trace("%s in cache.", format_size(total_size).c_str());

    if (total_size > params.m_max_cache_size)
    {
        ffmpegfs_trace("Pruning %s of oldest cache entries to limit cache size.", format_size(total_size - params.m_max_cache_size).c_str());
        if (ret == SQLITE_DONE)
        {
            int n = 0;
            for (vector<cache_key_t>::const_iterator it = keys.begin(); it != keys.end(); it++)
            {
                const cache_key_t & key = *it;

                ffmpegfs_trace("Pruning: %s Type: %s", key.first.c_str(), key.second.c_str());

                cache_t::iterator p = m_cache.find(key);
                if (p != m_cache.end())
                {
                    delete_entry(&p->second, CLOSE_CACHE_DELETE);
                }

                delete_info(key.first, key.second);

                total_size -= filesizes[n++];

                if (total_size <= params.m_max_cache_size)
                {
                    break;
                }
            }

            ffmpegfs_trace("%s left in cache.", format_size(total_size).c_str());
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
        ffmpegfs_trace("prune_disk_space() cannot determine free disk space: %s", strerror(errno));
        return false;
    }

    size_t free_bytes = buf.f_bfree * buf.f_bsize;

    ffmpegfs_trace("%s disk space before prune.", format_size(free_bytes).c_str());
    if (free_bytes < params.m_min_diskspace + predicted_filesize)
    {
        vector<cache_key_t> keys;
        vector<size_t> filesizes;
        sqlite3_stmt * stmt;
        const char * sql;

        sql = "SELECT filename, desttype, encoded_filesize FROM cache_entry ORDER BY access_time ASC;\n";

        sqlite3_prepare(m_cacheidx_db, sql, -1, &stmt, NULL);

        int ret = 0;
        while((ret = sqlite3_step(stmt)) == SQLITE_ROW)
        {
            const char *filename = (const char *) sqlite3_column_text(stmt, 0);
            const char *desttype = (const char *) sqlite3_column_text(stmt, 1);
            size_t size = (size_t)sqlite3_column_int64(stmt, 2);

            keys.push_back(make_pair(filename, desttype));
            filesizes.push_back(size);
        }

        ffmpegfs_trace("Pruning %s of oldest cache entries to keep disk space above %s limit...", format_size(params.m_min_diskspace + predicted_filesize - free_bytes).c_str(), format_size(params.m_min_diskspace).c_str());

        if (ret == SQLITE_DONE)
        {
            int n = 0;
            for (vector<cache_key_t>::const_iterator it = keys.begin(); it != keys.end(); it++)
            {
                const cache_key_t & key = *it;

                ffmpegfs_trace("Pruning: %s Type: %s", key.first.c_str(), key.second.c_str());

                cache_t::iterator p = m_cache.find(key);
                if (p != m_cache.end())
                {
                    delete_entry(&p->second, CLOSE_CACHE_DELETE);
                }

                delete_info(key.first, key.second);

                free_bytes += filesizes[n++];

                if (free_bytes >= params.m_min_diskspace + predicted_filesize)
                {
                    break;
                }
            }
            ffmpegfs_trace("Disk space after prune: %s", format_size(free_bytes).c_str());
        }
        else
        {
            ffmpegfs_error("Failed to execute select: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
        }

        sqlite3_finalize(stmt);
    }

    return true;
}

bool Cache::cache_maintenance(size_t predicted_filesize)
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
