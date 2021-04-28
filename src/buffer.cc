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

#include <algorithm>
#include <ostream>

#include "logging.h"

void Buffer::write(const std::vector<uint8_t>& data, bool extend_buffer) {
    main_data_.insert(main_data_.end(), data.begin(), data.end());
    if (main_data_.size() > static_cast<size_t>(end_offset_)) {
        if (extend_buffer) {
            end_offset_ = static_cast<std::ptrdiff_t>(main_data_.size());
        } else {
            main_data_.resize(end_offset_);
        }
    }
}

void Buffer::write_to(const std::vector<uint8_t>& data, std::ptrdiff_t offset) {
    std::copy(data.begin(), data.end(), main_data_.begin() + offset);
}

void Buffer::write_end(const std::vector<uint8_t>& data,
                       std::ptrdiff_t offset) {
    end_data_ = data;
    end_offset_ = offset;
}

void Buffer::copy_into(uint8_t* out_data, std::ptrdiff_t offset,
                       size_t size) const {
    if (!valid_bytes(offset, size)) {
        Log(ERROR) << "Invalid offset=" << offset << " size=" << size
                   << " in Buffer::copy_into.";
        return;
    }
    if (offset + size <= main_data_.size()) {
        std::copy_n(main_data_.begin() + offset, size, out_data);
    } else if (offset >= end_offset_) {
        std::copy_n(end_data_.begin() + offset - end_offset_, size, out_data);
    } else {
        size_t start_size = main_data_.size() - offset;
        uint8_t* next =
            std::copy_n(main_data_.begin() + offset, start_size, out_data);
        std::copy_n(end_data_.begin(), size - start_size, next);
    }
}

bool Buffer::valid_bytes(std::ptrdiff_t offset, size_t size) const {
    size_t end = offset + size;
    return offset >= 0 && end <= this->size() &&
           (end <= main_data_.size() || offset >= end_offset_ ||
            main_data_.size() == static_cast<size_t>(end_offset_));
}

size_t Buffer::max_valid_bytes(std::ptrdiff_t offset) const {
    if (static_cast<size_t>(offset) > size()) {
        return 0;
    }
    if (main_data_.size() == static_cast<size_t>(end_offset_) ||
        offset >= end_offset_) {
        // In either case, the whole rest of the Buffer is valid.
        return size() - offset;
    }
    if (static_cast<size_t>(offset) <= main_data_.size()) {
        // In this case, everything to the end of the main segment is valid.
        return main_data_.size() - offset;
    }
    // The offset is between the main and end segments, so nothing is valid.
    return 0;
}
