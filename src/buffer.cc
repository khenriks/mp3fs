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

#include <cstdlib>
#include <cstring>

#include "logging.h"


void Buffer::write(const std::vector<uint8_t>& data) {
    ensure_size(buffer_pos_ + data.size());
    std::copy(data.begin(), data.end(), data_.begin() + buffer_pos_);
    mark_valid(buffer_pos_, buffer_pos_ + data.size());
    buffer_pos_ += data.size();
}

void Buffer::write(const std::vector<uint8_t>& data, size_t offset) {
    ensure_size(offset + data.size());
    std::copy(data.begin(), data.end(), data_.begin() + offset);
    mark_valid(offset, offset + data.size());
}

void Buffer::ensure_size(size_t size) {
    if (data_.size() < size) {
        if ((size_t)end_bound_ == data_.size()) {
            end_bound_ = size;
        }
        data_.resize(size, 0);
    }
}

void Buffer::copy_into(uint8_t* out_data, size_t offset, size_t size) const {
    std::copy(data_.begin() + offset, data_.begin() + offset + size,
              out_data);
}

bool Buffer::valid_bytes(size_t offset, size_t size) const {
    std::streamoff end = offset + size;

    /*
     * Bytes are valid if [ offset,end ) lies within
     * [ 0,start_bound_ ) U [ end_bound,data_.size() )
     *
     * This requires that end <= data_.size(), and further that
     *   a) end <= start_bound_,
     *   b) offset >= end_bound_, or
     *   c) start_bound_ == end_bound_.
     */
    return end <= (std::streamoff)data_.size() &&
        (end <= start_bound_ || (std::streamoff)offset >= end_bound_ ||
         start_bound_ == end_bound_);
}

void Buffer::mark_valid(std::streamoff start, std::streamoff end) {
    if (start <= start_bound_) {
        if (end > start_bound_) {
            start_bound_ = end;
        }
    } else if (end >= end_bound_) {
        if (start < end_bound_) {
            end_bound_ = start;
        }
    } else {
        Log(ERROR) << "Cannot mark [" << start << "," << end << ") as valid "
        "with start_bound=" << start_bound_ << " and end_bound=" << end_bound_;
        start_bound_ = end;
    }
}
