/*
 * Encoder and decoder interfaces for mp3fs
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

#ifndef CODERS_H
#define CODERS_H

#include <cstdint>
#include <string>

#include "buffer.h"

/*
 * Metadata tag enum constants. These values are needed to coordinate
 * different tag names for different formats (FLAC vs. ID3 etc.). The
 * set_text_tag() method of Encoder shall accept these values to determine
 * which tag it should set. Each constant's name will consist of METATAG_ +
 * the vorbis comment version of the tag.
 */
enum {
    METATAG_TITLE,
    METATAG_ARTIST,
    METATAG_ALBUM,
    METATAG_GENRE,
    METATAG_DATE,
    METATAG_COMPOSER,
    METATAG_PERFORMER,
    METATAG_COPYRIGHT,
    METATAG_ENCODEDBY,
    METATAG_ORGANIZATION,
    METATAG_CONDUCTOR,
    METATAG_ALBUMARTIST,
    METATAG_TRACKNUMBER,
    METATAG_TRACKTOTAL,
    METATAG_DISCNUMBER,
    METATAG_DISCTOTAL,
    METATAG_ENCODER,
    METATAG_TRACKLENGTH,
    NUMBER_METATAG_FIELDS
};

/* Encoder class interface */
class Encoder {
public:
    virtual ~Encoder() { };

    virtual int set_stream_params(uint64_t num_samples, int sample_rate,
                                  int channels) = 0;
    virtual void set_text_tag(const int key, const char* value) = 0;
    virtual void set_picture_tag(const char* mime_type, int type,
                                 const char* description, const uint8_t* data,
                                 int data_length) = 0;
    virtual void set_gain_db(const double dbgain) = 0;
    void set_gain(double gainref, double album_gain, double track_gain);
    virtual int render_tag() = 0;
    virtual size_t get_actual_size() const = 0;
    virtual size_t calculate_size() const = 0;
    virtual int encode_pcm_data(const int32_t* const data[], int numsamples,
                                int sample_size) = 0;
    virtual int encode_finish() = 0;

    virtual bool no_partial_encode() { return true; }

    static Encoder* CreateEncoder(const std::string file_type, Buffer& buffer,
            size_t actual_size = 0);

    constexpr static double invalid_db = 1000.0;
};

/* Decoder class interface */
class Decoder {
public:
    virtual ~Decoder() { };

    virtual int open_file(const char* filename) = 0;
    /* The modified time of the decoder file */
    virtual time_t mtime() = 0;
    virtual int process_metadata(Encoder* encoder) = 0;
    virtual int process_single_fr(Encoder* encoder) = 0;

    static Decoder* CreateDecoder(const std::string file_type);
};

#endif
