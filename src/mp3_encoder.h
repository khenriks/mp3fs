/*
 * mp3 encoder class header for mp3fs
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

#ifndef MP3_ENCODER_H
#define MP3_ENCODER_H

#include "coders.h"

#include <map>

#include <id3tag.h>
#include <lame/lame.h>

class Mp3Encoder : public Encoder {
public:
    static const size_t id3v1_tag_length = 128;

    Mp3Encoder(size_t actual_size);
    ~Mp3Encoder();

    int set_stream_params(uint64_t num_samples, int sample_rate,
                          int channels);
    void set_text_tag(const int key, const char* value);
    void set_picture_tag(const char* mime_type, int type,
                         const char* description, const uint8_t* data,
                         int data_length);
    void set_gain_db(const double dbgain);
    int render_tag(Buffer& buffer);
    size_t get_actual_size() const;
    size_t calculate_size() const;
    int encode_pcm_data(const int32_t* const data[], int numsamples,
                        int sample_size, Buffer& buffer);
    int encode_finish(Buffer& buffer);

private:
    lame_t lame_encoder;
    size_t actual_size;    // Use this as the size instead of computing it.
    struct id3_tag* id3tag;
    size_t id3size;
    typedef std::map<int,const char*> meta_map_t;
    static const meta_map_t create_meta_map();
    static const meta_map_t metatag_map;
};

#endif
