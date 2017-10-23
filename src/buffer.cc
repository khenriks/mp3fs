/*
 * data buffer class source for mp3fs
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

#include "buffer.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <assert.h>

#include "transcode.h"

/* Initially Buffer is empty. It will be allocated as needed. */
Buffer::Buffer() :
    buffer_data(NULL),
    buffer_pos(0),
    buffer_size(0),
    filled_size(0)
{
}

/* If buffer_data was never allocated, this is a no-op. */
Buffer::~Buffer() {
    /* Have to work around OS X Mountain Lion bug */
    int olderrno = errno;
    free(buffer_data);
    errno = olderrno;
}

/*
 * Write data to the current position in the Buffer. The position pointer
 * will be updated.
 */
size_t Buffer::write(const uint8_t* data, size_t length) {
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
size_t Buffer::write(const uint8_t* data, size_t length, size_t offset) {
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
uint8_t* Buffer::write_prepare(size_t length) {
    if (reallocate(buffer_pos + length)) {
        if (filled_size < buffer_pos + length) {
            filled_size = buffer_pos + length;
        }
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
uint8_t* Buffer::write_prepare(size_t length, size_t offset) {
    if (reallocate(offset + length)) {
        if (filled_size < offset + length) {
            filled_size = offset + length;
        }
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
void Buffer::increment_pos(ptrdiff_t increment) {
    buffer_pos += increment;
}

bool Buffer::seek(size_t pos) {
    if (pos <= buffer_size) {
        buffer_pos = pos;
        return true;
    }
    else {
        buffer_pos = buffer_size;
        return false;
    }
}

/* Give the value of the internal position pointer. */
size_t Buffer::tell() const {
    return buffer_pos;
}

/* Give the value of the internal position pointer. */
size_t Buffer::size() const {
    return buffer_size;
}

/* Number of bytes written to buffer so far */
size_t Buffer::actual_size() const {
    return filled_size;
}

/* Copy buffered data into output buffer. */
void Buffer::copy_into(uint8_t* out_data, size_t offset, size_t size) const {
    //assert(buffer_size > offset + size);
    assert(buffer_data != NULL);
    if (buffer_size < offset)
    {
        return;
    }

    if (buffer_size < offset + size)
    {
        size = buffer_size - offset - 1;
    }
    memcpy(out_data, buffer_data + offset, size);
}

/*
 * Ensure the allocation has at least size bytes available. If not,
 * reallocate memory to make more available. Fill the newly allocated memory
 * with zeroes.
 */
bool Buffer::reallocate(size_t size) {
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

        //mp3fs_debug("Buffer reallocate: %p -> %p ; %lu -> %lu.", buffer_data, newdata, buffer_size, size);

        buffer_data = newdata;
        buffer_size = size;
    }

    return true;
}
