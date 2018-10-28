/*
 * mp3 encoder class source for mp3fs
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

#include "codecs/mp3_encoder.h"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "logging.h"

/* Copied from lame */
#define MAX_VBR_FRAME_SIZE 2880

/* Keep these items in static scope. */
namespace  {

/* Callback functions for each type of lame message callback */
static void lame_error(const char *fmt, va_list list) {
    log_with_level(ERROR, "LAME: ", fmt, list);
}
static void lame_msg(const char *fmt, va_list list) {
    log_with_level(ERROR, "LAME: ", fmt, list);
}
static void lame_debug(const char *fmt, va_list list) {
    log_with_level(DEBUG, "LAME: ", fmt, list);
}

}

/*
 * Create MP3 encoder. Do not set any parameters specific to a
 * particular file. Currently error handling is poor. If we run out
 * of memory, these routines will fail silently.
 */
Mp3Encoder::Mp3Encoder(Buffer& buffer, size_t _actual_size) :
actual_size(_actual_size), buffer_(buffer) {
    id3tag = id3_tag_new();

    Log(DEBUG) << "LAME ready to initialize.";

    lame_encoder = lame_init();

    set_text_tag(METATAG_ENCODER, PACKAGE_NAME);

    /* Set lame parameters. */
    if (params.vbr) {
       lame_set_VBR(lame_encoder, vbr_mt);
       lame_set_VBR_q(lame_encoder, params.quality);
       lame_set_VBR_max_bitrate_kbps(lame_encoder, params.bitrate);
       lame_set_bWriteVbrTag(lame_encoder, 1);
    } else {
       lame_set_quality(lame_encoder, params.quality);
       lame_set_brate(lame_encoder, params.bitrate);
       lame_set_bWriteVbrTag(lame_encoder, 0);
    }
    lame_set_errorf(lame_encoder, &lame_error);
    lame_set_msgf(lame_encoder, &lame_msg);
    lame_set_debugf(lame_encoder, &lame_debug);
}

/*
 * Destroy private encode data. libid3tag asserts that id3tag is nonzero,
 * so we have to check ourselves to avoid this case.
 */
Mp3Encoder::~Mp3Encoder() {
    if (id3tag) {
        id3_tag_delete(id3tag);
    }
    lame_close(lame_encoder);
}

/*
 * Set pcm stream parameters to be used by LAME encoder. This should be
 * called as soon as the information is available and must be called
 * before encode_pcm_data can be called.
 */
int Mp3Encoder::set_stream_params(uint64_t num_samples, int sample_rate,
                                   int channels) {
    lame_set_num_samples(lame_encoder, num_samples);
    lame_set_in_samplerate(lame_encoder, sample_rate);
    lame_set_num_channels(lame_encoder, channels);

    Log(DEBUG) << "LAME partially initialized.";

    /* Initialise encoder */
    if (lame_init_params(lame_encoder) == -1) {
        Log(ERROR) << "lame_init_params failed.";
        return -1;
    }

    Log(DEBUG) << "LAME initialized.";

    /*
     * Set the length in the ID3 tag, as this is the most convenient place
     * to do it.
     */
    std::ostringstream tempstr;
    tempstr << num_samples*1000/sample_rate;
    set_text_tag(METATAG_TRACKLENGTH, tempstr.str().c_str());

    return 0;
}

/*
 * Set an ID3 text tag (one whose name begins with "T") to have the
 * specified value. This can be called multiple times with the same key,
 * and the tag will receive multiple values for the tag, as allowed by the
 * standard. The tag is assumed to be encoded in UTF-8.
 */
void Mp3Encoder::set_text_tag(const int key, const char* value) {
    if (!value) {
        return;
    }

    meta_map_t::const_iterator it = metatag_map.find(key);

    if (it != metatag_map.end()) {
        struct id3_frame* frame = id3_tag_findframe(id3tag, it->second, 0);
        if (!frame) {
            frame = id3_frame_new(it->second);
            id3_tag_attachframe(id3tag, frame);

            id3_field_settextencoding(id3_frame_field(frame, 0),
                                      ID3_FIELD_TEXTENCODING_UTF_8);
        }

        id3_ucs4_t* ucs4 = id3_utf8_ucs4duplicate((id3_utf8_t *)value);
        if (ucs4) {
            id3_field_addstring(id3_frame_field(frame, 1), ucs4);
            free(ucs4);
        }
    /* Special handling for track or disc numbers. */
    } else if (key == METATAG_TRACKNUMBER || key == METATAG_TRACKTOTAL
               || key == METATAG_DISCNUMBER || key == METATAG_DISCTOTAL) {
        const char* tagname;
        if (key == METATAG_TRACKNUMBER || key == METATAG_TRACKTOTAL) {
            tagname = "TRCK";
        } else {
            tagname = "TPOS";
        }
        struct id3_frame* frame = id3_tag_findframe(id3tag, tagname, 0);
        const id3_latin1_t* lat;
        id3_latin1_t* tofree = 0;
        if (frame) {
            const id3_ucs4_t* pre = id3_field_getstrings(id3_frame_field(frame, 1), 0);
            tofree = id3_ucs4_latin1duplicate(pre);
            lat = tofree;
        } else {
            frame = id3_frame_new(tagname);
            id3_tag_attachframe(id3tag, frame);
            id3_field_settextencoding(id3_frame_field(frame, 0),
                                      ID3_FIELD_TEXTENCODING_UTF_8);
            lat = (const id3_latin1_t*)"";
        }
        std::ostringstream tempstr;
        if (key == METATAG_TRACKNUMBER || key == METATAG_DISCNUMBER) {
            tempstr << value << lat;
        } else {
            tempstr << lat << "/" << value;
        }
        id3_ucs4_t* ucs4 =
            id3_latin1_ucs4duplicate((id3_latin1_t*)tempstr.str().c_str());
        if (ucs4) {
            id3_field_setstrings(id3_frame_field(frame, 1), 1, &ucs4);
            free(ucs4);
        }
        if (tofree) {
            free(tofree);
        }
    }
}

/* Set an ID3 picture ("APIC") tag. */
void Mp3Encoder::set_picture_tag(const char* mime_type, int type,
                                 const char* description, const uint8_t* data,
                                 int data_length) {
    struct id3_frame* frame = id3_frame_new("APIC");
    id3_tag_attachframe(id3tag, frame);

    id3_field_settextencoding(id3_frame_field(frame, 0),
                              ID3_FIELD_TEXTENCODING_UTF_8);
    id3_field_setlatin1(id3_frame_field(frame, 1),
                        (id3_latin1_t*)mime_type);
    id3_field_setint(id3_frame_field(frame, 2), type);
    id3_field_setbinarydata(id3_frame_field(frame, 4), data, data_length);

    id3_ucs4_t* ucs4 = id3_utf8_ucs4duplicate((id3_utf8_t *)description);
    if (ucs4) {
        id3_field_setstring(id3_frame_field(frame, 3), ucs4);
        free(ucs4);
    }
}

/*
 * Set MP3 gain value in decibels. For MP3, there is no standard tag that can
 * be used, so the value is set directly as a gain in the encoder. The pow
 * formula comes from
 * http://replaygain.hydrogenaudio.org/proposal/player_scale.html
 */
void Mp3Encoder::set_gain_db(const double dbgain) {
    Log(DEBUG) << "LAME setting gain to " <<  dbgain << ".";
    lame_set_scale(lame_encoder, (float)pow(10.0, dbgain/20));
}

/*
 * Render the ID3 tag into the referenced Buffer. This should be the first
 * thing to go into the Buffer. The ID3v1 tag will also be written 128
 * bytes from the calculated end of the buffer. It has a fixed size.
 */
int Mp3Encoder::render_tag() {
    /*
     * Disable ID3 compression because it hardly saves space and some
     * players don't like it.
     * Also add 12 bytes of padding at the end, because again some players
     * are buggy.
     * Some players = iTunes
     */
    id3_tag_options(id3tag, ID3_TAG_OPTION_COMPRESSION, 0);
    id3_tag_setlength(id3tag, id3_tag_render(id3tag, nullptr) + 12);

    // write v2 tag
    id3size = id3_tag_render(id3tag, nullptr);
    std::vector<uint8_t> tag24(id3size);
    id3_tag_render(id3tag, tag24.data());
    buffer_.write(tag24);

    // Write v1 tag at end of buffer.
    id3_tag_options(id3tag, ID3_TAG_OPTION_ID3V1, ~0);
    std::vector<uint8_t> tag1(id3v1_tag_length);
    id3_tag_render(id3tag, tag1.data());
    buffer_.write(tag1, calculate_size() - id3v1_tag_length);

    return 0;
}

/*
 * Get the actual number of bytes in the encoded file, i.e. without any
 * padding. Valid only after encode_finish() has been called.
 */
size_t Mp3Encoder::get_actual_size() const {
    return actual_size;
}

/*
 * Properly calculate final file size. This is the sum of the size of
 * ID3v2, ID3v1, and raw MP3 data. This is theoretically only approximate
 * but in practice gives excellent answers, usually exactly correct.
 * Cast to 64-bit int to avoid overflow.
 */
size_t Mp3Encoder::calculate_size() const {
    if (actual_size != 0) {
        return actual_size;
    } else if (params.vbr) {
        return id3size + id3v1_tag_length + MAX_VBR_FRAME_SIZE
        + (uint64_t)lame_get_totalframes(lame_encoder)*144*params.bitrate*10
        / (lame_get_in_samplerate(lame_encoder)/100);
    } else {
        return id3size + id3v1_tag_length
        + (uint64_t)lame_get_totalframes(lame_encoder)*144*params.bitrate*10
        / (lame_get_out_samplerate(lame_encoder)/100);
    }
}

/*
 * Encode the given PCM data into the given Buffer. This function must not
 * be called before the encoder has finished initialization with
 * set_stream_params(). It should be called after the ID3 tag has been
 * rendered into the Buffer with render_tag(). The data is given as a
 * multidimensional array of channel data in the format previously
 * specified, as 32-bit right-aligned signed integers. Right-alignment
 * means that the range of values from each sample should be
 * -2**(sample_size-1) to 2**(sample_size-1)-1. This is not coincidentally
 * the format used by the FLAC library.
 */
int Mp3Encoder::encode_pcm_data(const int32_t* const data[], int numsamples,
                                int sample_size) {
    /*
     * We need to properly resample input data to a format LAME wants. LAME
     * requires samples in a C89 sized type, left aligned (i.e. scaled to
     * the maximum value of the type) and we cannot be sure for example how
     * large an int is. We require it be at least 32 bits on all platforms
     * that will run mp3fs, and rescale to the appropriate size. Cast
     * first to avoid integer overflow.
     */
    std::vector<int> lbuf(numsamples), rbuf(numsamples);
    for (int i=0; i<numsamples; ++i) {
        lbuf[i] = (int)data[0][i] << (sizeof(int)*8 - sample_size);
        /* ignore rbuf for mono data */
        if (lame_get_num_channels(lame_encoder) > 1) {
            rbuf[i] = (int)data[1][i] << (sizeof(int)*8 - sample_size);
        }
    }

    std::vector<uint8_t> vbuffer(5*numsamples/4 + 7200);

    int len = lame_encode_buffer_int(lame_encoder, &lbuf[0], &rbuf[0],
                                     numsamples, vbuffer.data(),
                                     (int)vbuffer.size());
    if (len < 0) {
        return -1;
    }
    vbuffer.resize(len);

    buffer_.write(vbuffer);

    return 0;
}

/*
 * Encode any remaining PCM data in LAME internal buffers to the given
 * Buffer. This should be called after all input data has already been
 * passed to encode_pcm_data().
 */
int Mp3Encoder::encode_finish() {
    std::vector<uint8_t> vbuffer(7200);

    int len = lame_encode_flush(lame_encoder, vbuffer.data(),
                                (int)vbuffer.size());
    if (len < 0) {
        return -1;
    }
    vbuffer.resize(len);

    buffer_.write(vbuffer);
    actual_size = buffer_.tell() + id3v1_tag_length;

    /*
     * Write the VBR tag data at id3size bytes after the beginning. lame
     * already put dummy bytes here when lame_init_params() was called.
     */
    if (params.vbr) {
        std::vector<uint8_t> tail(MAX_VBR_FRAME_SIZE);
        size_t vbr_tag_size = lame_get_lametag_frame(lame_encoder, tail.data(),
                MAX_VBR_FRAME_SIZE);
        if (vbr_tag_size > MAX_VBR_FRAME_SIZE) {
           return -1;
        }
        buffer_.write(tail, id3size);
    }

    return len;
}

/*
 * This map contains the association from the standard values in the enum in
 * coders.h to ID3 values.
 */
const Mp3Encoder::meta_map_t Mp3Encoder::metatag_map {
    {METATAG_TITLE, "TIT2"},
    {METATAG_ARTIST, "TPE1"},
    {METATAG_ALBUM, "TALB"},
    {METATAG_GENRE, "TCON"},
    {METATAG_DATE, "TDRC"},
    {METATAG_COMPOSER, "TCOM"},
    {METATAG_PERFORMER, "TOPE"},
    {METATAG_COPYRIGHT, "TCOP"},
    {METATAG_ENCODEDBY, "TENC"},
    {METATAG_ORGANIZATION, "TPUB"},
    {METATAG_CONDUCTOR, "TPE3"},
    {METATAG_ALBUMARTIST, "TPE2"},
    {METATAG_ENCODER, "TSSE"},
    {METATAG_TRACKLENGTH, "TLEN"},
};
