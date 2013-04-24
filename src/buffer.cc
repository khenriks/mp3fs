/*
 * data buffer class source for mp3fs
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

#include "buffer.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

Buffer::Buffer() : buffer_data(0), buffer_pos(0), buffer_size(0) { }

Buffer::~Buffer() {
    free(buffer_data);
}

int Buffer::write(uint8_t* data, unsigned int length) {
    uint8_t* write_ptr = write_prepare(length);
    if (!write_ptr) {
        return 0;
    }
    memcpy(write_ptr, data, length);
    increment_pos(length);

    return length;
}

int Buffer::write(uint8_t* data, unsigned int length,
                  unsigned int offset) {
    uint8_t* write_ptr = write_prepare(length, offset);
    if (!write_ptr) {
        return 0;
    }
    memcpy(write_ptr, data, length);

    return length;
}

uint8_t* Buffer::write_prepare(unsigned int length) {
    if (reallocate(buffer_pos + length)) {
        return buffer_data + buffer_pos;
    } else {
        return NULL;
    }
}

uint8_t* Buffer::write_prepare(unsigned int length, unsigned int offset) {
    if (reallocate(offset + length)) {
        return buffer_data + offset;
    } else {
        return NULL;
    }
}

/*
 * Increment the location of the internal pointer. This cannot fail and so
 * returns void. It does not ensure the position is valid memory because
 * that is done by the write_prepare methods via reallocate.
 */

void Buffer::increment_pos(int increment) {
    buffer_pos += increment;
}

bool Buffer::reallocate(unsigned int size) {
    if (size > buffer_size) {
        uint8_t* newdata = (uint8_t*)realloc(buffer_data, size);
        if (!newdata) {
            return false;
        }
        /*
         * For some reason, errno is set to ENOMEM when the data is moved
         * as a result of the realloc call. This works around that
         * behavior.
         */
        if (errno == ENOMEM) {
            errno = 0;
        }

        buffer_data = newdata;
        buffer_size = size;
    }

    return true;
}
