/*
 * data buffer class header for ffmpegfs
 *
 * Copyright (C) 2013 K. Henriksson
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

#ifndef CACHE_H
#define CACHE_H

#pragma once

#include "buffer.h"

#include <cstddef>
#include <string>
#include <map>

#include <sqlite3.h>

using namespace std;

typedef struct
{
    string          m_filename;
    char            m_target_format[11];
    bool            m_enable_ismv;
    unsigned int    m_audiobitrate;
    unsigned int    m_audiosamplerate;
    unsigned int    m_videobitrate;
    unsigned int    m_videowidth;
    unsigned int    m_videoheight;
    bool            m_deinterlace;
    size_t          m_predicted_filesize;
    size_t          m_encoded_filesize;
    bool            m_finished;
    bool            m_error;
    int             m_errno;
    int             m_averror;
    time_t          m_creation_time;
    time_t          m_access_time;
    time_t          m_file_time;
    uint64_t        m_file_size;
} t_cache_info;

class Cache_Entry;

class Cache
{
    typedef map<string, Cache_Entry *> cache_t;

    friend class Cache_Entry;

public:
    Cache();
    ~Cache();

    Cache_Entry *open(const char *filename);
    bool close(Cache_Entry **cache_entry, int flags = CLOSE_CACHE_NOOPT);

    bool load_index();
#ifdef HAVE_SQLITE_CACHEFLUSH
    bool flush_index();
#endif // HAVE_SQLITE_CACHEFLUSH

    void lock();
    void unlock();

    bool cache_maintenance(size_t predicted_filesize = 0);

    bool prune_expired();
    bool prune_cache_size();
    bool prune_disk_space(size_t predicted_filesize);

protected:
    bool read_info(t_cache_info & cache_info);
    bool write_info(const t_cache_info & cache_info);
    bool delete_info(const string &filename);

    Cache_Entry* create_entry(const string & filename);
    bool delete_entry(Cache_Entry **cache_entry, int flags);

    void close_index();

private:
    pthread_mutex_t m_mutex;
    string          m_cacheidx_file;
    sqlite3*        m_cacheidx_db;
    sqlite3_stmt *  m_cacheidx_select_stmt;
    sqlite3_stmt *  m_cacheidx_insert_stmt;
    sqlite3_stmt *  m_cacheidx_delete_stmt;
    cache_t         m_cache;
};

#endif
