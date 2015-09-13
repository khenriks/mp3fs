/*
 * METADATA_BLOCK_PICTURE handler class source for mp3fs
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

#include "picture.h"
#include "transcode.h"

/*
 * Construct a METADATA_BLOCK_PICTURE handler and initialise the base64 decoder
 */
Picture::Picture() {
    mp3fs_debug("Picture handler: Initializing.");
    const int b64_buffer_size = 64 * 1024;
    b64_decoder = new base64::decoder(b64_buffer_size);
}

/*
 * Destroy the METADATA_BLOCK_PICTURE handler and free resources
 */
Picture::~Picture() {
    delete b64_decoder;
    delete[] plaintext;
    delete[] mime_type.data.string;
    delete[] description.data.string;
    delete[] picture.data.string;
    mp3fs_debug("Picture handler: Closed.");
}

/*
 * Decode a base64 encoded string into plaintext
 */
int Picture::decode(const std::string* encoded) {
    mp3fs_debug("Picture handler: Base64-decoding a string.");

    plaintext = new char[3 * encoded->length() / 4 + 1];
    length = b64_decoder->decode(encoded->c_str(), (int)encoded->length(), plaintext);

    if (length <= 0) {
        mp3fs_debug("Picture handler: Failed to decode the metadata block.");
        return EXIT_FAILURE;
    }

    int retval = EXIT_FAILURE;

    /*
     * Set up the metadata
     */
    mp3fs_debug("Picture handler: Preparing the type metadata.");
    type.start = 0;
    type.length = 4;

    if (type.start + type.length <= length) {
        type.data.number = sequence_to_uint(type.start);

        mp3fs_debug("Picture handler: Preparing the mime_type metadata.");
        int length_start = type.start + type.length;
        mime_type.start = length_start + 4;

        if (mime_type.start <= length) {
            mime_type.length = sequence_to_uint(length_start);

            if (mime_type.start + mime_type.length <= length) {
                sequence_to_str(&mime_type, true);

                mp3fs_debug("Picture handler: Preparing the description metadata.");
                length_start = mime_type.start + mime_type.length;
                description.start = length_start + 4;

                if (description.start <= length) {
                    description.length = sequence_to_uint(length_start);

                    if (description.start + description.length) {
                        sequence_to_str(&description, true);

                        mp3fs_debug("Picture handler: Preparing the picture data metadata.");
                        length_start = description.start + description.length + 4 * 4;
                        picture.start = length_start + 4;

                        if (picture.start <= length) {
                            picture.length = sequence_to_uint(length_start);

                            if (picture.start + picture.length <= length) {
                                sequence_to_str(&picture, false);

                                retval = EXIT_SUCCESS;
                            }
                        }
                    }
                }
            }
        }
    }

    /*
     * Free the plaintext and reset its length
     */
    delete[] plaintext;
    plaintext = NULL;
    length = 0;

    return retval;
}

/*
 * Convert a big endian char sequence representing a number into an unsigned int.
 * Intended to be called from within int Picture::decode(const std::string*).
 */
unsigned int Picture::sequence_to_uint(int start) {
    mp3fs_debug("Picture handler: Converting a sequence into an unsigned int.");
    const char len = 4;
    union int_representation {
            unsigned int as_uint;
            char as_char[len];
    } number;

    for (char i = 0; i < len; ++i) {
        number.as_char[len - 1 - i] = plaintext[start + i];
    }

    return number.as_uint;
}

/*
 * Convert a char sequence into a string, null-terminated if you want to.
 * Intended to be called from within int Picture::decode(const std::string*).
 */
void Picture::sequence_to_str(metadata* m, bool is_cstr) {
    mp3fs_debug("Picture handler: Converting a sequence into a string.");
    delete[] m->data.string;
    m->data.string = is_cstr ? new char[m->length + 1] : new char[m->length];

    for (int i = 0; i < m->length; ++i) {
        m->data.string[i] = plaintext[m->start + i];
    }

    if (is_cstr)
        m->data.string[m->length] = '\0';
}

/*
 * Get the METADATA_BLOCK_PICTURE type
 */
int Picture::get_type() {
    mp3fs_debug("Picture handler: Getting the type.");
    return type.data.number;
}

/*
 * Get the METADATA_BLOCK_PICTURE mime type
 */
const char* Picture::get_mime_type() {
    mp3fs_debug("Picture handler: Getting the mime_type.");
    return mime_type.data.string;
}

/*
 * Get the METADATA_BLOCK_PICTURE description
 */
const char* Picture::get_description() {
    mp3fs_debug("Picture handler: Getting the description.");
    return description.data.string;
}

/*
 * Get the METADATA_BLOCK_PICTURE data length
 */
int Picture::get_data_length() {
    mp3fs_debug("Picture handler: Getting the data's length.");
    return picture.length;
}

/*
 * Get the METADATA_BLOCK_PICTURE data
 */
const uint8_t* Picture::get_data() {
    mp3fs_debug("Picture handler: Getting the data.");
    return (uint8_t*)picture.data.string;
}
