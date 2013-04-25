/*
 * data buffer class header for mp3fs
 *
 * Copyright (C) 2013 Kristofer Henriksson
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

class Buffer {
public:
    Buffer();
    ~Buffer();

    unsigned long write(uint8_t* data, unsigned long length);
    unsigned long write(uint8_t* data, unsigned long length,
                        unsigned long offset);
    uint8_t* write_prepare(unsigned long length);
    uint8_t* write_prepare(unsigned long length, unsigned long offset);
    void increment_pos(long increment);
    unsigned long tell() const;
private:
    bool reallocate(unsigned long size);
    uint8_t* buffer_data;
    unsigned long buffer_pos;
    unsigned long buffer_size;
};

#endif
