/*
 * cache class header for ffmpegfs
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

#include <stdint.h>
#include <string>

#define CACHE_CHECK_BIT(mask, var)  ((mask) == (mask & (var)))

#define CLOSE_CACHE_NOOPT   0x00                        // Dummy, do nothing special
#define CLOSE_CACHE_FREE    0x01                        // Free memory for cache entry
#define CLOSE_CACHE_DELETE  (0x02 | CLOSE_CACHE_FREE)   // Delete cache entry, will unlink cached file! Implies CLOSE_CACHE_FREE.

using namespace std;

class Buffer
{
public:
    explicit Buffer(const string & filename);
    virtual ~Buffer();

    bool open(bool erase_cache = false);                // Open cache, if erase_cache = true delete old file before opening
    bool close(int flags = CLOSE_CACHE_NOOPT);
    bool flush();
    bool clear();
    bool reserve(size_t size);
    size_t write(const uint8_t* data, size_t length);
    bool seek(size_t pos);
    size_t tell() const;
    size_t size() const;
    size_t buffer_watermark() const;
    bool copy(uint8_t* out_data, size_t offset, size_t bufsize);

    void lock();
    void unlock();

    const string & filename() const;
    const string & cachefile() const;

protected:
    bool remove_file();

private:
    uint8_t* write_prepare(size_t length);
    void increment_pos(ptrdiff_t increment);
    bool reallocate(size_t newsize);

private:
    pthread_mutex_t m_mutex;
    const string &  m_filename;
    string          m_cachefile;
    size_t          m_buffer_pos;           // Read/write position
    size_t          m_buffer_watermark;     // Number of bytes in buffer
    volatile bool   m_is_open;
    size_t          m_buffer_size;          // Current buffer size
    uint8_t *       m_buffer;
    int             m_fd;
};

#endif
