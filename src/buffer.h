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
     *
     * If the given range is not valid, an error will be logged.
     */
    void copy_into(uint8_t* out_data, size_t offset, size_t size) const;

    /**
     * Return whether the given number of bytes at the given offset are valid
     * (have been already filled).
     */
    bool valid_bytes(size_t offset, size_t size) const;
private:
    /**
     * Ensure the Buffer has at least the size given.
     */
    void ensure_size(size_t size);

    /**
     * Mark the given range of bytes in the buffer as valid. This is treated as
     * a semi-closed interval [start,end). If this interval is not contiguous
     * with the two existing intervals, log an error and treat this as extending
     * the first interval.
     *
     * Valid bytes in the buffer are used to track which portions have actually
     * been populated with data, and which are simply capacity allocations. The
     * following ranges determine whether a byte is valid:
     * [0,start_bound_) are valid.
     * [start_bound_,end_bound_) are invalid.
     * [end_bound_,data_.size()) are valid.
     *
     * This simple model corresponds to current transcoding patterns, where some
     * number of bytes at the beginning and end have been filled, but others
     * have not.
     */
    void mark_valid(std::streamoff start, std::streamoff end);

    std::vector<uint8_t> data_;
    std::streamoff buffer_pos_ = 0;

    // start_bound_ indicates the first invalid byte in the buffer. All previous
    // bytes are considered to be valid.
    std::streamoff start_bound_ = 0;
    // end_bound_ indicates the first valid byte at the end of the buffer. This
    // and all following bytes are considered to be valid.
    std::streamoff end_bound_ = 0;
};

#endif
