/*
 * FFmpeg transcoder class header for ffmpegfs
 *
 * Copyright (C) 2017 Norbert Schlia (nschlia@oblivion-software.de)
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

#ifndef CACHE_ENTRY_H
#define CACHE_ENTRY_H

#pragma once

#include "cache.h"
#include "id3v1tag.h"

#include <string>

class Buffer;

struct Cache_Entry
{
    friend class Cache;

protected:
    explicit Cache_Entry(Cache *owner, const string & filename);
    virtual ~Cache_Entry();

public:
    bool open(bool create_cache = true);
    bool flush();
    void clear(int fetch_file_time = true);
    size_t size() const;
    time_t age() const;
    time_t last_access() const;
    bool expired() const;
    bool suspend_timeout() const;
    bool decode_timeout() const;
    const string & filename();
    bool update_access(bool bUpdateDB = false);

    void lock();
    void unlock();

    int ref_count() const;

protected:
    bool close(int flags);
    void close_buffer(int flags);

    bool read_info();
    bool write_info();
    bool delete_info();

protected:
    Cache *         m_owner;
    pthread_mutex_t m_mutex;

    int             m_ref_count;

public:
    Buffer *        m_buffer;
    bool            m_is_decoding;
    pthread_t       m_thread_id;

    t_cache_info    m_cache_info;

    ID3v1           m_id3v1;
};

#endif // CACHE_ENTRY_H
