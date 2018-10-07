/*
 * FLAC-format PICTURE handler class source for mp3fs
 *
 * Copyright (C) 2017 K. Henriksson
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

#include "codecs/picture.h"

#include <arpa/inet.h>

#include "logging.h"

/* Decode binary picture data. */
bool Picture::decode() {
    std::string picture_data_str;

    if (!consume_decode_uint32(type) ||
        !consume_decode_string(mime_type) ||
        !consume_decode_string(description) ||
        !consume_no_decode(16) ||
        !consume_decode_string(picture_data_str)) {
        Log(ERROR) << "Couldn't decode picture data as valid data.";
        return false;
    }

    picture_data.assign(picture_data_str.c_str(),
                        picture_data_str.c_str() + picture_data_str.size());

    return true;
}

/* Decode a 32-bit integer from the picture data and advance pointer. */
bool Picture::consume_decode_uint32(uint32_t& out) {
    if (data_off_ + 4 > data_.size()) return false;

    out = ntohl(*(uint32_t*)(data_.data() + data_off_));

    data_off_ += 4;

    return true;
}

/* Decode a string from the picture data and advance pointer. */
bool Picture::consume_decode_string(std::string& out) {
    uint32_t len;
    if (!consume_decode_uint32(len)) return false;

    if (data_off_ + len > data_.size()) return false;

    out.assign(data_.data() + data_off_, len);

    data_off_ += len;

    return true;
}
