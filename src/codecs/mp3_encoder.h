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

#ifndef MP3FS_CODECS_MP3_ENCODER_H_
#define MP3FS_CODECS_MP3_ENCODER_H_

#include <lame/lame.h>

#include <cstddef>
#include <cstdint>
#include <map>

#include "codecs/coders.h"
#include "mp3fs.h"

class Buffer;

class Mp3Encoder : public Encoder {
 public:
    static const size_t id3v1_tag_length = 128;

    explicit Mp3Encoder(Buffer* buffer);
    ~Mp3Encoder() override;

    int set_stream_params(uint64_t num_samples, int sample_rate,
                          int channels) override;
    void set_text_tag(int key, const char* value) override;
    void set_picture_tag(const char* mime_type, int type,
                         const char* description, const uint8_t* data,
                         unsigned int data_length) override;
    void set_gain_db(double dbgain) override;
    int render_tag(size_t file_size) override;
    size_t calculate_size() const override;
    int encode_pcm_data(const int32_t* const data[], unsigned int numsamples,
                        unsigned int sample_size) override;
    int encode_finish() override;

    /*
     * The Xing data (which is pretty close to the beginning of the
     * file) cannot be determined until the entire file is encoded, so
     * transcode the entire file for any read.
     */
    bool no_partial_encode() override { return params.vbr != 0; }

 private:
    lame_t lame_encoder;
    struct id3_tag* id3tag;
    size_t id3size = 0;
    Buffer* buffer_;
    using meta_map_t = std::map<int, const char*>;
    static const meta_map_t metatag_map;
};

#endif  // MP3FS_CODECS_MP3_ENCODER_H_
