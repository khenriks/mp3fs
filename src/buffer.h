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

#ifndef MP3FS_BUFFER_H_
#define MP3FS_BUFFER_H_

#include <cstddef>
#include <cstdint>
#include <vector>

class Buffer {
 public:
    Buffer() = default;
    ~Buffer() = default;

    /**
     * Write data to the end of the Buffer's main segment.
     *
     * If enough data is provided, the main segment will run into the end
     * segment. If extend_buffer is false, the data written will be truncated to
     * avoid this. Otherwise, the buffer length will be increased to accommodate
     * the data.
     */
    void write(const std::vector<uint8_t>& data, bool extend_buffer);

    /**
     * Write data to a specified position in the Buffer. Results in undefined
     * behavior if this section of the Buffer had not previously been filled in.
     */
    void write_to(const std::vector<uint8_t>& data, std::ptrdiff_t offset);

    /**
     * Write data that will be placed at the end of the Buffer. This overwrites
     * data (if any) already written at the end previously. The offset parameter
     * and data size determine the total size of the Buffer. It is permissible
     * to write an empty buffer with an offset; doing so sets the total size of
     * the Buffer when there is no trailing data.
     */
    void write_end(const std::vector<uint8_t>& data, std::ptrdiff_t offset);

    /**
     * Give the size of data already written in the main segment.
     */
    size_t tell() const { return main_data_.size(); }

    /**
     * Retrieve the total size of the buffer.
     */
    size_t size() const { return end_offset_ + end_data_.size(); }

    /**
     * Copy data of the given size and at the given offset from the buffer to
     * the location given by out_data.
     *
     * If the given range is not valid, an error will be logged.
     */
    void copy_into(uint8_t* out_data, std::ptrdiff_t offset, size_t size) const;

    /**
     * Return whether the given number of bytes at the given offset are valid
     * (have been already filled).
     *
     * Bytes are valid if the range lies fully within main_data_, fully within
     * end_data_ (subject to end_offset_), or overlaps the two and
     * main_data_.size() == end_offset_.
     */
    bool valid_bytes(std::ptrdiff_t offset, size_t size) const;

    /**
     * Return the maximum number of bytes that can be read from offset. This is
     * the maximum size such that valid_bytes(offset, size) == true.
     */
    size_t max_valid_bytes(std::ptrdiff_t offset) const;

    /**
     * Move end of main segment to start of end segment.
     */
    void extend() { main_data_.resize(end_offset_); }

    /**
     * Move end segment to end of main segment.
     */
    void truncate() {
        end_offset_ = static_cast<std::ptrdiff_t>(main_data_.size());
    }

 private:
    std::vector<uint8_t> main_data_;
    std::vector<uint8_t> end_data_;
    std::ptrdiff_t end_offset_ = 0;
};

#endif  // MP3FS_BUFFER_H_
