/*
 * FFMPEG transcoder class header for mp3fs
 *
 * Copyright (C) 2017 Norbert Schlia (nschlia@oblivon-software.de)
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

#include <string>

class Buffer;
class FFMPEG_Transcoder;
struct ID3v1;

struct Cache_Entry {

    friend class Cache;

protected:
    explicit Cache_Entry(const char *filename);
    virtual ~Cache_Entry();
    
public:
    std::string info_file() const;
    bool read_info();
    bool write_info();
    bool open(bool create_cache = true);
    bool close(bool erase_cache = false);
    bool flush();
    time_t mtime() const;
    size_t calculate_size() const;
    const ID3v1 * id3v1tag() const;
    time_t age() const;
    time_t last_access() const;
    bool expired() const;
    bool suspend_timeout() const;
    bool decode_timeout() const;

protected:
    void reset(int fetch_file_time = true);

public:
    Buffer *            m_buffer;
    std::string         m_filename;
    std::string         m_cachefile;
    bool                m_is_decoding;
    pthread_t           m_thread_id;
    int                 m_ref_count;

    struct
    {
        size_t          m_encoded_filesize;
        bool            m_finished;
        bool            m_error;
        time_t          m_creation_time;
        time_t          m_access_time;
        time_t          m_file_time;
    } m_info;

    //protected:
    FFMPEG_Transcoder * m_transcoder;
};

#endif // CACHE_ENTRY_H
