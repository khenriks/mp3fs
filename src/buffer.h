/*
 * data buffer class header for mp3fs
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

#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>

#include <cstddef>

class Buffer {
public:
    Buffer();
    ~Buffer();

    size_t write(const uint8_t* data, size_t length);
    size_t write(const uint8_t* data, size_t length, size_t offset);
    uint8_t* write_prepare(size_t length);
    uint8_t* write_prepare(size_t length, size_t offset);
    void increment_pos(ptrdiff_t increment);
    size_t tell() const;
    void copy_into(uint8_t* out_data, size_t offset, size_t size) const;
private:
    bool reallocate(size_t size);
    uint8_t* buffer_data;
    size_t buffer_pos;
    size_t buffer_size;
};

#endif
