/*
 * Encoder and decoder interfaces for mp3fs
 *
 * Copyright (C) 2013 Kristofer Henriksson
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

#ifndef CODERS_H
#define CODERS_H

#include <stdint.h>

#include "buffer.h"

class Encoder {
public:
    virtual int set_stream_params(uint64_t num_samples, int sample_rate,
                                  int channels) = 0;
    virtual void set_text_tag(const char* key, const char* value) = 0;
    virtual void set_picture_tag(const char* mime_type, int type,
                                 const char* description, const uint8_t* data,
                                 int data_length) = 0;
    virtual int render_tag(Buffer& buffer) = 0;
    virtual unsigned long calculate_size() const = 0;
    virtual int encode_pcm_data(const int32_t* data[], int numsamples,
                                 int channels, Buffer& buffer) = 0;
    virtual int encode_finish(Buffer& buffer) = 0;
};

class Decoder {

};

#endif
