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

#include <cstddef>
#include <cstdint>
#include <ios>
#include <vector>

class Buffer {
public:
    Buffer() {};
    ~Buffer() {};

    /**
     * Write data to the current position in the Buffer. The position pointer
     * will be updated.
     */
    void write(const std::vector<uint8_t>& data);

    /**
     * Write data to a specified position in the Buffer. The position pointer
     * will not be updated.
     */
    void write(const std::vector<uint8_t>& data, size_t offset);

    /**
     * Give the value of the internal position pointer.
     */
    size_t tell() const { return buffer_pos_; }

    /**
     * Copy data of the given size and at the given offset from the buffer to
     * the location given by out_data.
     */
    void copy_into(uint8_t* out_data, size_t offset, size_t size) const;
private:
    /**
     * Ensure the Buffer has at least the size given.
     */
    void ensure_size(size_t size);

    std::vector<uint8_t> data_;
    std::streamoff buffer_pos_ = 0;
};

#endif
