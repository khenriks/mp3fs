/*
 * METADATA_BLOCK_PICTURE handler class header for mp3fs
 *
 * Copyright (C) 2015 Thomas Schwarzenberger
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

#ifndef METADATA_BLOCK_PICTURE_H
#define METADATA_BLOCK_PICTURE_H

#include <b64/decode.h>

#include <string>

class Picture {

public:
    Picture();
    ~Picture();

    int decode(const std::string* encoded);
    int get_type();
    const char* get_mime_type();
    const char* get_description();
    int get_data_length();
    const uint8_t* get_data();

private:
    base64::decoder* b64_decoder = NULL;
    char* plaintext = NULL;
    int length = 0;

    typedef struct meta {
        int start = 0;
        int length = 0;
        union block_data {
            unsigned int number;
            char* string = NULL;
        } data;
    } metadata;

    metadata type, mime_type, description, picture;

    unsigned int sequence_to_uint(int start);
    void sequence_to_str(metadata* m, bool is_cstr);
};


#endif
