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

/* Initially Buffer is empty. It will be allocated as needed. */
Buffer::Buffer() : buffer_data(0), buffer_pos(0), buffer_size(0) { }

/* If buffer_data was never allocated, this is a no-op. */
Buffer::~Buffer() {
    free(buffer_data);
}

/*
 * Write data to the current position in the Buffer. The position pointer
 * will be updated.
 */
unsigned long Buffer::write(const uint8_t* data, unsigned long length) {
    uint8_t* write_ptr = write_prepare(length);
    if (!write_ptr) {
        return 0;
    }
    memcpy(write_ptr, data, length);
    increment_pos(length);

    return length;
}

/*
 * Write data to a specified position in the Buffer. The position pointer
 * will not be updated.
 */
unsigned long Buffer::write(const uint8_t* data, unsigned long length,
                            unsigned long offset) {
    uint8_t* write_ptr = write_prepare(length, offset);
    if (!write_ptr) {
        return 0;
    }
    memcpy(write_ptr, data, length);

    return length;
}

/*
 * Ensure the Buffer has sufficient space for a quantity of data and
 * return a pointer where the data may be written. The position pointer
 * should be updated afterward with increment_pos().
 */
uint8_t* Buffer::write_prepare(unsigned long length) {
    if (reallocate(buffer_pos + length)) {
        return buffer_data + buffer_pos;
    } else {
        return NULL;
    }
}

/*
 * Ensure the Buffer has sufficient space for a quantity of data written
 * to a particular location and return a pointer where the data may be
 * written.
 */
uint8_t* Buffer::write_prepare(unsigned long length, unsigned long offset) {
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
void Buffer::increment_pos(long increment) {
    buffer_pos += increment;
}

/* Give the value of the internal position pointer. */
unsigned long Buffer::tell() const {
    return buffer_pos;
}

/* Copy buffered data into output buffer. */
void Buffer::copy_into(uint8_t* out_data, unsigned long offset,
                       unsigned long size) const {
    memcpy(out_data, buffer_data + offset, size);
}

/*
 * Ensure the allocation has at least size bytes available. If not,
 * reallocate memory to make more available. Fill the newly allocated memory
 * with zeroes.
 */
bool Buffer::reallocate(unsigned long size) {
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
        /* Set new allocation to zero. */
        memset(newdata + buffer_size, 0, size - buffer_size);

        buffer_data = newdata;
        buffer_size = size;
    }

    return true;
}
