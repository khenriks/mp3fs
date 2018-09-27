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

#include "transcode.h"


void Buffer::write(const std::vector<uint8_t>& data) {
    ensure_size(buffer_pos_ + data.size());
    std::copy(data.begin(), data.end(), data_.begin() + buffer_pos_);
    buffer_pos_ += data.size();
}

void Buffer::write(const std::vector<uint8_t>& data, size_t offset) {
    ensure_size(offset + data.size());
    std::copy(data.begin(), data.end(), data_.begin() + offset);
}

void Buffer::ensure_size(size_t size) {
    if (data_.size() < size) {
        data_.resize(size, 0);
    }
}

void Buffer::copy_into(uint8_t* out_data, size_t offset, size_t size) const {
    std::copy(data_.begin() + offset, data_.begin() + offset + size,
              out_data);
}
