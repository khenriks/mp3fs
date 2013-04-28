/*
 * mp3 encoder class source for mp3fs
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

#include "mp3_encoder.h"

#include <inttypes.h>

#include <cstdlib>
#include <vector>

#include "transcode.h"

/* Keep these items in static scope. */
namespace  {

/*
 * Print messages from lame. We cannot easily prepend a string to indicate
 * that the message comes from lame, so we need to render it ourselves.
 */
static void lame_print(int priority, const char *fmt, va_list list) {
    char* msg;
    if (vasprintf(&msg, fmt, list) != -1) {
        syslog(priority, "LAME: %s", msg);
        free(msg);
    }
}

/* Callback functions for each type of lame message callback */
static void lame_error(const char *fmt, va_list list) {
    lame_print(LOG_ERR, fmt, list);
}
static void lame_msg(const char *fmt, va_list list) {
    lame_print(LOG_INFO, fmt, list);
}
static void lame_debug(const char *fmt, va_list list) {
    lame_print(LOG_DEBUG, fmt, list);
}

}

/*
 * Create MP3 encoder. Do not set any parameters specific to a
 * particular file. Currently error handling is poor. If we run out
 * of memory, these routines will fail silently.
 */
Mp3Encoder::Mp3Encoder() {
    id3tag = id3_tag_new();

    mp3fs_debug("LAME ready to initialize.");

    lame_encoder = lame_init();

    set_text_tag(METATAG_ENCODER, PACKAGE_NAME);

    /* Set lame parameters. */
    lame_set_quality(lame_encoder, params.quality);
    lame_set_brate(lame_encoder, params.bitrate);
    lame_set_bWriteVbrTag(lame_encoder, 0);
    lame_set_errorf(lame_encoder, &lame_error);
    lame_set_msgf(lame_encoder, &lame_msg);
    lame_set_debugf(lame_encoder, &lame_debug);
}

/*
 * Destroy private encode data. libid3tag asserts that id3tag is nonzero,
 * so we have to check ourselves to avoid this case.
 */
Mp3Encoder::~Mp3Encoder() {
    if (!id3tag) {
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

    mp3fs_debug("LAME partially initialized.");

    /* Initialise encoder */
    if (lame_init_params(lame_encoder) == -1) {
        mp3fs_debug("lame_init_params failed.");
        return -1;
    }

    mp3fs_debug("LAME initialized.");

    /*
     * Set the length in the ID3 tag, as this is the most convenient place
     * to do it.
     */
    char tmpstr[10];
    snprintf(tmpstr, 10, "%" PRIu64, num_samples*1000/sample_rate);
    set_text_tag(METATAG_TRACKLENGTH, tmpstr);

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
 * Render the ID3 tag into the referenced Buffer. This should be the first
 * thing to go into the Buffer. The ID3v1 tag will also be written 128
 * bytes from the calculated end of the buffer. It has a fixed size.
 */
int Mp3Encoder::render_tag(Buffer& buffer) {
    /*
     * Disable ID3 compression because it hardly saves space and some
     * players don't like it.
     * Also add 12 bytes of padding at the end, because again some players
     * are buggy.
     * Some players = iTunes
     */
    id3_tag_options(id3tag, ID3_TAG_OPTION_COMPRESSION, 0);
    id3_tag_setlength(id3tag, id3_tag_render(id3tag, 0) + 12);

    mp3fs_debug("Ready to write tag.");

    // grow buffer and write v2 tag
    uint8_t* write_ptr = buffer.write_prepare(id3_tag_render(id3tag, 0));
    if (!write_ptr) {
        mp3fs_debug("Failed to write tag.");
        return -1;
    }
    id3size = id3_tag_render(id3tag, write_ptr);
    buffer.increment_pos(id3size);

    /* Write v1 tag at end of buffer. */
    id3_tag_options(id3tag, ID3_TAG_OPTION_ID3V1, ~0);
    write_ptr = buffer.write_prepare(128, calculate_size() - 128);
    id3_tag_render(id3tag, write_ptr);

    mp3fs_debug("Tag written.");

    return 0;
}

/*
 * Properly calculate final file size. This is the sum of the size of
 * ID3v2, ID3v1, and raw MP3 data. This is theoretically only approximate
 * but in practice gives excellent answers, usually exactly correct;
 */
unsigned long Mp3Encoder::calculate_size() const {
    return id3size + 128
    + lame_get_totalframes(lame_encoder)*144*params.bitrate*10
    / (lame_get_out_samplerate(lame_encoder)/100);
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
int Mp3Encoder::encode_pcm_data(const int32_t* data[], int numsamples,
                                 int sample_size, Buffer& buffer) {
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

    uint8_t* write_ptr = buffer.write_prepare(5*numsamples/4 + 7200);
    if (!write_ptr) {
        return -1;
    }

    int len = lame_encode_buffer_int(lame_encoder, &lbuf[0], &rbuf[0],
                                     numsamples, write_ptr,
                                     5*numsamples/4 + 7200);
    if (len < 0) {
        return -1;
    }

    buffer.increment_pos(len);

    return 0;
}

/*
 * Encode any remaining PCM data in LAME internal buffers to the given
 * Buffer. This should be called after all input data has already been
 * passed to encode_pcm_data().
 */
int Mp3Encoder::encode_finish(Buffer& buffer) {
    uint8_t* write_ptr = buffer.write_prepare(7200);
    if (!write_ptr) {
        return -1;
    }

    int len = lame_encode_flush(lame_encoder, write_ptr, 7200);
    if (len < 0) {
        return -1;
    }

    buffer.increment_pos(len);

    return 0;
}

/*
 * This function creates the metadata tag map from the standard values in the
 * enum in coders.h to ID3 values. It will be called only once to set the
 * metatag_map static variable.
 */
const Mp3Encoder::meta_map_t Mp3Encoder::create_meta_map() {
    meta_map_t m;

    m[METATAG_TITLE] = "TIT2";
    m[METATAG_ARTIST] = "TPE1";
    m[METATAG_ALBUM] = "TALB";
    m[METATAG_GENRE] = "TCON";
    m[METATAG_DATE] = "TDRC";
    m[METATAG_COMPOSER] = "TCOM";
    m[METATAG_PERFORMER] = "TOPE";
    m[METATAG_COPYRIGHT] = "TCOP";
    m[METATAG_ENCODEDBY] = "TENC";
    m[METATAG_ORGANIZATION] = "TPUB";
    m[METATAG_CONDUCTOR] = "TPE3";
    m[METATAG_ALBUMARTIST] = "TPE2";
    m[METATAG_ENCODER] = "TSSE";
    m[METATAG_TRACKLENGTH] = "TLEN";

    return m;
}

const Mp3Encoder::meta_map_t Mp3Encoder::metatag_map
    = Mp3Encoder::create_meta_map();
