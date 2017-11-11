/*
 * cache class header for mp3fs
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

#ifndef BUFFER_H
#define BUFFER_H

#pragma once

//https://www.safaribooksonline.com/library/view/linux-system-programming/0596009585/ch04s03.html
//https://gist.github.com/marcetcheverry/991042
#define _USE_DISK

#include <stdint.h>
#include <cstddef>
#ifndef _USE_DISK
#include <vector>
#else
#include <string>
#endif

class Buffer {
public:
    explicit Buffer(const std::string & filename, const std::string & cachefile);
    virtual ~Buffer();

    std::string cache_file() const;
    bool open();
    bool close(bool erase_cache = false);
    bool flush();
    bool reserve(size_t size);
    size_t write(const uint8_t* data, size_t length);
    bool seek(size_t pos);
    size_t tell() const;
    size_t size() const;
    size_t buffer_watermark() const;
    void copy(uint8_t* out_data, size_t offset, size_t bufsize);

private:
    uint8_t* write_prepare(size_t length);
    void increment_pos(ptrdiff_t increment);
    bool reallocate(size_t newsize);

private:
    pthread_mutex_t         mutex;
    const std::string &     m_filename;
    const std::string &     m_cachefile;
    size_t                  m_buffer_pos;           // Read/write position
    size_t                  m_buffer_watermark;     // Number of bytes in buffer
#ifdef _USE_DISK
    volatile bool           m_is_open;
    size_t                  m_buffer_size;          // Current buffer size
    uint8_t *      	    m_buffer;
    int                     m_fd;
#else
    std::vector<uint8_t>    m_buffer;
#endif
};

#endif
