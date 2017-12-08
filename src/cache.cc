/*
 * Cache controller class for mp3fs
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

Cache::~Cache() {

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
            mp3fs_error("Error creating cache directory '%s': %s", cachepath, strerror(errno));
            throw false;
        }

        // initialise engine
        if (SQLITE_OK != (ret = sqlite3_initialize()))
        {
            mp3fs_error("Failed to initialise SQLite3 library: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }
        // open connection to a DB
        if (SQLITE_OK != (ret = sqlite3_open_v2(m_cacheidx_file.c_str(), &m_cacheidx_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)))
        {
            mp3fs_error("Failed to initialise SQLite3 connection: %d, %s", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        // Create table and index if nit existing
        sql =   "CREATE TABLE IF NOT EXISTS cache_entry\n"
                "(\n"
                "id                INTEGER PRIMARY KEY ASC,\n"
                "filename          TEXT UNIQUE NOT NULL,\n"
                "target_format     CHAR(10) NOT NULL,\n"
                "encoded_filesize  UNSIGNED BIG INT NOT NULL,\n"
                "finished          BOOLEAN NOT NULL,\n"
                "error             BOOLEAN NOT NULL,\n"
                "creation_time     DATETIME NOT NULL,\n"
                "access_time       DATETIME NOT NULL,\n"
                "file_time         DATETIME NOT NULL,\n"
                "file_size         UNSIGNED BIG INT NOT NULL\n"
                ");\n"
                "CREATE UNIQUE INDEX IF NOT EXISTS \"filename_idx\" ON \"cache_entry\" (\"filename\" ASC);\n";

        if (SQLITE_OK != (ret = sqlite3_exec(m_cacheidx_db, sql, callback, 0, &errmsg)))
        {
            mp3fs_error("SQLite3 exec error: %d, %s", ret, errmsg);
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
                "(filename, target_format, encoded_filesize, finished, error, creation_time, access_time, file_time, file_size) VALUES\n"
                "(?, ?, ?, ?, ?, datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), datetime(?, 'unixepoch'), ?);\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_insert_stmt, NULL)))
        {
            mp3fs_error("Failed to prepare insert: %d, %s\n", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "SELECT target_format, encoded_filesize, finished, error, strftime('%s', creation_time), strftime('%s', access_time), strftime('%s', file_time), file_size FROM cache_entry WHERE filename = ?\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_select_stmt, NULL)))
        {
            mp3fs_error("Failed to prepare select: %d, %s\n", ret, sqlite3_errmsg(m_cacheidx_db));
            throw false;
        }

        sql =   "DELETE FROM cache_entry WHERE filename = ?\n";

        if (SQLITE_OK != (ret = sqlite3_prepare_v2(m_cacheidx_db, sql, -1, &m_cacheidx_delete_stmt, NULL)))
        {
            mp3fs_error("Failed to prepare delete: %d, %s\n", ret, sqlite3_errmsg(m_cacheidx_db));
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
            mp3fs_error("SQLite3 cache flush error: %d, %s", ret, sqlite3_errstr(ret));
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
        mp3fs_error("SQLite3 select statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_select_stmt) == 1);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_select_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
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
            cache_info.m_encoded_filesize   = sqlite3_column_int64(m_cacheidx_select_stmt, 1);
            cache_info.m_finished           = sqlite3_column_int(m_cacheidx_select_stmt, 2);
            cache_info.m_error              = sqlite3_column_int(m_cacheidx_select_stmt, 3);
            cache_info.m_creation_time      = sqlite3_column_int64(m_cacheidx_select_stmt, 4);
            cache_info.m_access_time        = sqlite3_column_int64(m_cacheidx_select_stmt, 5);
            cache_info.m_file_time          = sqlite3_column_int64(m_cacheidx_select_stmt, 6);
            cache_info.m_file_size          = sqlite3_column_int64(m_cacheidx_select_stmt, 7);
        }
        else if (ret != SQLITE_DONE)
        {
            mp3fs_error("Sqlite 3 could not step (execute) insert stmt: %d, %s", ret, sqlite3_errstr(ret));
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
        mp3fs_error("SQLite3 select statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_insert_stmt) == 9);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_insert_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_insert_stmt, 2, cache_info.m_target_format, -1, NULL)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 3, cache_info.m_encoded_filesize)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int(m_cacheidx_insert_stmt, 4, cache_info.m_finished)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int(m_cacheidx_insert_stmt, 5, cache_info.m_error)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 6, cache_info.m_creation_time)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 7, cache_info.m_access_time)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 8, cache_info.m_file_time)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        if (SQLITE_OK != (ret = sqlite3_bind_int64(m_cacheidx_insert_stmt, 9, cache_info.m_file_size)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_insert_stmt);

        if (ret != SQLITE_DONE)
        {
            mp3fs_error("Sqlite 3 could not step (execute) select statement: %d, %s", ret, sqlite3_errstr(ret));
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

bool Cache::delete_info(const t_cache_info & cache_info)
{
    int ret;
    bool success = true;

    if (m_cacheidx_delete_stmt == NULL)
    {
        mp3fs_error("SQLite3 delete statement not open");
        return false;
    }

    lock();

    try
    {
        assert(sqlite3_bind_parameter_count(m_cacheidx_delete_stmt) == 1);

        if (SQLITE_OK != (ret = sqlite3_bind_text(m_cacheidx_delete_stmt, 1, cache_info.m_filename.c_str(), -1, NULL)))
        {
            mp3fs_error("SQLite3 select error: %d, %s", ret, sqlite3_errstr(ret));
            throw false;
        }

        ret = sqlite3_step(m_cacheidx_delete_stmt);

        if (ret != SQLITE_DONE)
        {
            mp3fs_error("Sqlite 3 could not step (execute) delete statement: %d, %s", ret, sqlite3_errstr(ret));
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
    if (p == m_cache.end()) {
        mp3fs_debug("Created new transcoder for file '%s'.", sanitised_name.c_str());
        cache_entry = new Cache_Entry(this, sanitised_name);
        if (cache_entry == NULL)
        {
            mp3fs_error("Out of memory for file '%s'.", sanitised_name.c_str());
            return NULL;
        }
        m_cache.insert(std::make_pair(sanitised_name, cache_entry));
    } else {
        mp3fs_debug("Reusing cached transcoder for file '%s'.", sanitised_name.c_str());
        cache_entry = p->second;
    }

    return cache_entry;
}

void Cache::close(Cache_Entry **cache_entry, bool erase_cache)
{
    if ((*cache_entry) == NULL)
    {
        return;
    }

    std::string filename((*cache_entry)->filename());
    if ((*cache_entry)->m_cache_info.m_error || (!(*cache_entry)->m_is_decoding && !(*cache_entry)->m_cache_info.m_finished))
    {
        cache_t::iterator p = m_cache.find(filename);
        if (p == m_cache.end()) {
            mp3fs_warning("Unable to delete unknown cache entry for file '%s'.", filename.c_str());
        } else {
            mp3fs_debug("Deleted cache entry for file '%s'.", filename.c_str());
            m_cache.erase(p);

            (*cache_entry)->close(erase_cache);

            delete *cache_entry;
        }

        *cache_entry = NULL;
    }
    else
    {
        //(*cache_entry)->flush();
        (*cache_entry)->close();

        mp3fs_debug("Keeping transcoder for file '%s'.", filename.c_str());
    }
}

void Cache::lock()
{
    pthread_mutex_lock(&m_mutex);
}

void Cache::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}
