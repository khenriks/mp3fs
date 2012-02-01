/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2011 Kristofer Henriksson
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

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>

#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#include <lame/lame.h>
#include <id3tag.h>

#include "transcode.h"

/* Functions for dealing with the buffer */

/*
 * Prepare the buffer to accept data and return a pointer to a location
 * where data may be written.
 */
uint8_t* buffer_write_prepare(struct mp3_buffer* buffer, int len) {
    uint8_t* newdata;
    if (buffer->size < buffer->pos + len) {
        newdata = realloc(buffer->data, buffer->pos + len);
        /*
         * For some reason, errno is set to ENOMEM when the data is moved
         * as a result of the realloc call. This works around that
         * behavior.
         */
        if (newdata && errno == ENOMEM) {
            errno = 0;
        }
        if (!newdata) {
            return NULL;
        }

        buffer->data = newdata;
        buffer->size = buffer->pos + len;
    }

    return buffer->data + buffer->pos;
}

/*
 * Write data to the buffer. This makes use of the buffer_write_prepare
 * function.
 */
int buffer_write(struct mp3_buffer* buffer, uint8_t* data, int len) {
    uint8_t* write_ptr;
    write_ptr = buffer_write_prepare(buffer, len);
    if (!write_ptr) {
        return 0;
    }
    memcpy(write_ptr, data, len);
    buffer->pos += len;

    return len;
}

/*******************************************************************
 CALLBACKS and HELPERS for LAME and FLAC
*******************************************************************/

/* build an id3 frame */
struct id3_frame *make_frame(const char *name, const char *data) {
    struct id3_frame *frame;
    id3_ucs4_t       *ucs4;

    frame = id3_frame_new(name);

    ucs4 = id3_utf8_ucs4duplicate((id3_utf8_t *)data);
    if (ucs4) {
        id3_field_settextencoding(&frame->fields[0],
                                  ID3_FIELD_TEXTENCODING_UTF_8);
        id3_field_setstrings(&frame->fields[1], 1, &ucs4);
        free(ucs4);
    }
    return frame;
}

/* return a vorbis comment tag */
const char *get_tag(const FLAC__StreamMetadata *metadata, const char *name) {
    int idx;
    const FLAC__StreamMetadata_VorbisComment *comment;
    comment = &metadata->data.vorbis_comment;
    idx = FLAC__metadata_object_vorbiscomment_find_entry_from(metadata, 0,
                                                              name);
    if (idx<0) return NULL;

    return (const char *) (comment->comments[idx].entry + strlen(name) + 1);
}

void set_tag(const FLAC__StreamMetadata *metadata, struct id3_tag *id3tag,
             const char *id3name, const char *vcname) {
    const char *str = get_tag(metadata, vcname);
    if (str)
        id3_tag_attachframe(id3tag, make_frame(id3name, str));
}

/* set id3 picture tag from FLAC picture block */
void set_picture_tag(const FLAC__StreamMetadata *metadata,
                     struct id3_tag *id3tag) {
    const FLAC__StreamMetadata_Picture *picture;
    struct id3_frame *frame;
    id3_ucs4_t       *ucs4;

    picture = &metadata->data.picture;

    /*
     * There hardly seems a point in separating out these into a different
     * function since it would need access to picture anyway.
     */

    frame = id3_frame_new("APIC");
    id3_tag_attachframe(id3tag, frame);

    ucs4 = id3_utf8_ucs4duplicate((id3_utf8_t *)picture->description);
    if (ucs4) {
        id3_field_settextencoding(&frame->fields[0],
                                  ID3_FIELD_TEXTENCODING_UTF_8);
        id3_field_setlatin1(id3_frame_field(frame, 1),
                            (id3_latin1_t*)picture->mime_type);
        id3_field_setint(id3_frame_field(frame, 2), picture->type);
        id3_field_setstring(&frame->fields[3], ucs4);
        id3_field_setbinarydata(id3_frame_field(frame, 4), picture->data,
                                picture->data_length);
        free(ucs4);
    }
}

/* divide one integer by another and round off the result */
int divideround(long long one, int another) {
    int result;

    result = one / another;
    if (one % another >= another / 2) result++;

    return result;
}

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

/* Callback for FLAC errors */
static void error_cb(const FLAC__StreamDecoder *decoder,
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data) {
    mp3fs_error("FLAC error: %s",
                FLAC__StreamDecoderErrorStatusString[status]);
}

/* FLAC write callback */
static FLAC__StreamDecoderWriteStatus
write_cb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
         const FLAC__int32 *const buffer[], void *client_data) {
    int len, i;
    struct transcoder* trans = (struct transcoder*)client_data;
    int lbuf[FLAC_BLOCKSIZE], rbuf[FLAC_BLOCKSIZE];
    uint8_t* write_ptr;

    /*
     * We need to properly resample input data to a common format in order
     * to pass it on to LAME. LAME requires samples in a C89 sized type,
     * and we cannot be sure for example how large an int is. We assume it
     * is at least 32 bits on all platforms that will run mp3fs, and hope
     * for the best.
     */

    for (i=0; i<frame->header.blocksize; i++) {
        lbuf[i] = buffer[0][i] <<
            (sizeof(int)*8 - frame->header.bits_per_sample);
        // ignore rbuf for mono sources
        if (frame->header.channels > 1) {
            rbuf[i] = buffer[1][i] <<
                (sizeof(int)*8 - frame->header.bits_per_sample);
        }
    }

    write_ptr = buffer_write_prepare(&trans->buffer, BUFSIZE);
    if (!write_ptr) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    len = lame_encode_buffer_int(trans->encoder, lbuf, rbuf,
                                 frame->header.blocksize, write_ptr, BUFSIZE);
    if (len < 0) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    trans->buffer.pos += len;

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void meta_cb(const FLAC__StreamDecoder *decoder,
                    const FLAC__StreamMetadata *metadata, void *client_data) {
    char tmpstr[10];
    float dbgain = 0;
    float filegainref;
    FLAC__StreamMetadata_StreamInfo info;
    struct transcoder* trans = (struct transcoder*)client_data;

    switch (metadata->type) {
        case FLAC__METADATA_TYPE_STREAMINFO:
            info = metadata->data.stream_info;

            /* set the length in the id3tag */
            snprintf(tmpstr, 10, "%" PRIu64,
                info.total_samples*1000/info.sample_rate);
            id3_tag_attachframe(trans->id3tag, make_frame("TLEN", tmpstr));

            /* Use the data in STREAMINFO to set lame parameters. */
            lame_set_num_samples(trans->encoder, info.total_samples);
            lame_set_in_samplerate(trans->encoder, info.sample_rate);
            lame_set_num_channels(trans->encoder, info.channels);

            break;
        case FLAC__METADATA_TYPE_VORBIS_COMMENT:

            /* set the common stuff */
            set_tag(metadata, trans->id3tag, ID3_FRAME_TITLE, "TITLE");
            set_tag(metadata, trans->id3tag, ID3_FRAME_ARTIST, "ARTIST");
            set_tag(metadata, trans->id3tag, ID3_FRAME_ALBUM, "ALBUM");
            set_tag(metadata, trans->id3tag, ID3_FRAME_GENRE, "GENRE");
            set_tag(metadata, trans->id3tag, ID3_FRAME_YEAR, "DATE");

            /* less common, but often present */
            set_tag(metadata, trans->id3tag, "COMM", "DESCRIPTION");
            set_tag(metadata, trans->id3tag, "TCOM", "COMPOSER");
            set_tag(metadata, trans->id3tag, "TOPE", "PERFORMER");
            set_tag(metadata, trans->id3tag, "TCOP", "COPYRIGHT");
            set_tag(metadata, trans->id3tag, "WXXX", "LICENSE");
            set_tag(metadata, trans->id3tag, "TENC", "ENCODED_BY");
            set_tag(metadata, trans->id3tag, "TPUB", "ORGANIZATION");
            set_tag(metadata, trans->id3tag, "TPE3", "CONDUCTOR");

            /* album artist can be stored in different fields */
            if (get_tag(metadata, "ALBUMARTIST")) {
                set_tag(metadata, trans->id3tag, "TPE2", "ALBUMARTIST");
            } else if (get_tag(metadata, "ALBUM ARTIST")) {
                set_tag(metadata, trans->id3tag, "TPE2", "ALBUM ARTIST");
            }

            /* set the track/total */
            if (get_tag(metadata, "TRACKNUMBER")) {
                if (get_tag(metadata, "TRACKTOTAL")) {
                    snprintf(tmpstr, 10, "%s/%s",
                             get_tag(metadata, "TRACKNUMBER"),
                             get_tag(metadata, "TRACKTOTAL"));
                } else {
                    snprintf(tmpstr, 10, "%s",
                             get_tag(metadata, "TRACKNUMBER"));
                }
                id3_tag_attachframe(trans->id3tag,
                                    make_frame(ID3_FRAME_TRACK, tmpstr));
            }

            /* set the disc/total, also less common */
            if (get_tag(metadata, "DISCNUMBER")) {
                if (get_tag(metadata, "DISCTOTAL")) {
                    snprintf(tmpstr, 10, "%s/%s",
                             get_tag(metadata, "DISCNUMBER"),
                             get_tag(metadata, "DISCTOTAL"));
                } else {
                    snprintf(tmpstr, 10, "%s",
                             get_tag(metadata, "DISCNUMBER"));
                }
                id3_tag_attachframe(trans->id3tag,
                                    make_frame("TPOS", tmpstr));
            }

            /*
             * Use the Replay Gain tag to set volume scaling. Obey the
             * options for gainmode and gainref.
             */
            /* Read reference value from file. */
            if (get_tag(metadata, "REPLAYGAIN_REFERENCE_LOUDNESS")) {
                filegainref = atof(get_tag(metadata,
                                           "REPLAYGAIN_REFERENCE_LOUDNESS"));
            } else {
                filegainref = 89.0;
            }
            /* Determine what gain value should be applied. */
            if (params.gainmode == 1 &&
                get_tag(metadata, "REPLAYGAIN_ALBUM_GAIN")) {
                dbgain = atof(get_tag(metadata, "REPLAYGAIN_ALBUM_GAIN"));
            } else if ((params.gainmode == 1 || params.gainmode == 2) &&
                       get_tag(metadata, "REPLAYGAIN_TRACK_GAIN")) {
                dbgain = atof(get_tag(metadata, "REPLAYGAIN_TRACK_GAIN"));
            }
            /*
             * Adjust the gain value, if any, by the change in gain
             * reference value. The powf formula comes from
             * http://replaygain.hydrogenaudio.org/proposal/player_scale.html
             */
            if (dbgain) {
                dbgain += params.gainref - filegainref;
                lame_set_scale(trans->encoder, powf(10, dbgain/20));
            }

            break;
        case FLAC__METADATA_TYPE_PICTURE:

            /* add a picture tag for each picture block */
            set_picture_tag(metadata, trans->id3tag);

            break;
        default:
            break;
    }
}

/* Allocate and initialize the transcoder */

struct transcoder* transcoder_new(char *flacname) {
    struct transcoder* trans;
    uint8_t* write_ptr;

    mp3fs_debug("Creating transcoder object for %s", flacname);

    /* Allocate transcoder structure */
    trans = malloc(sizeof(struct transcoder));
    if (!trans) {
        goto trans_fail;
    }

    /* Initialize to zero */
    memset(trans, 0, sizeof(struct transcoder));

    /* Initialize ID3 tag */
    trans->id3tag = id3_tag_new();
    if (!trans->id3tag) {
        goto id3_fail;
    }

    id3_tag_attachframe(trans->id3tag, make_frame("TSSE", "MP3FS"));

    /* Create and initialise decoder */
    trans->decoder = FLAC__stream_decoder_new();
    if (trans->decoder == NULL) {
        goto flac_fail;
    }

    FLAC__stream_decoder_set_metadata_respond(trans->decoder,
                                    FLAC__METADATA_TYPE_VORBIS_COMMENT);
    FLAC__stream_decoder_set_metadata_respond(trans->decoder,
                                              FLAC__METADATA_TYPE_PICTURE);

    mp3fs_debug("FLAC ready to initialize.");

    if (FLAC__stream_decoder_init_file(trans->decoder, flacname,
                                       &write_cb, &meta_cb, &error_cb,
                                       (void *)trans) !=
        FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        goto flac_init_fail;
    }

    mp3fs_debug("FLAC initialized successfully.");

    mp3fs_debug("LAME ready to initialize.");

    /* Create encoder */
    trans->encoder = lame_init();
    if (trans->encoder == NULL) {
        goto lame_fail;
    }

    lame_set_quality(trans->encoder, params.quality);
    lame_set_brate(trans->encoder, params.bitrate);
    lame_set_bWriteVbrTag(trans->encoder, 0);
    lame_set_errorf(trans->encoder, &lame_error);
    lame_set_msgf(trans->encoder, &lame_msg);
    lame_set_debugf(trans->encoder, &lame_debug);

    /*
     * Process metadata. This will fill in the id3tag and the remaining
     * lame parameters. If the function fails, the FLAC is invalid.
     */
    if (!FLAC__stream_decoder_process_until_end_of_metadata(trans->decoder)) {
        mp3fs_debug("FLAC is invalid.");
        goto lame_fail;
    }

    mp3fs_debug("LAME partially initialized.");

    /* Initialise encoder */
    if (lame_init_params(trans->encoder) == -1) {
        goto lame_init_fail;
    }

    mp3fs_debug("LAME initialized.");

    /* Now we have to render our id3tag so that we know how big the total
     * file is. We write the id3v2 tag directly into the front of the
     * buffer. The id3v1 tag is written into a fixed 128 byte buffer (it
     * is a fixed size)
     */

    /*
     * Disable ID3 compression because it hardly saves space and some
     * players don't like it.
     * Also add 12 bytes of padding at the end, because again some players
     * are buggy.
     * Some players = iTunes
     */
    id3_tag_options(trans->id3tag, ID3_TAG_OPTION_COMPRESSION, 0);
    id3_tag_setlength(trans->id3tag, id3_tag_render(trans->id3tag, 0)+12);

    mp3fs_debug("Ready to write tag.");

    // grow buffer and write v2 tag
    write_ptr = buffer_write_prepare(&trans->buffer,
                                     id3_tag_render(trans->id3tag, 0));
    if (!write_ptr) {
        goto write_tag_fail;
    }
    trans->buffer.pos += id3_tag_render(trans->id3tag, write_ptr);

    // store v1 tag
    id3_tag_options(trans->id3tag, ID3_TAG_OPTION_ID3V1, ~0);
    id3_tag_render(trans->id3tag, trans->id3v1tag);

    mp3fs_debug("Tag written.");

    /*
     * Properly calculate final file size. This is the sum of the size of
     * ID3v2, ID3v1, and raw MP3 data.
     */
    trans->totalsize = trans->buffer.pos + 128
        + lame_get_totalframes(trans->encoder)*144*params.bitrate*10
        / (lame_get_out_samplerate(trans->encoder)/100);

    id3_tag_delete(trans->id3tag);
    return trans;

write_tag_fail:
lame_init_fail:
    lame_close(trans->encoder);

lame_fail:
flac_init_fail:
    FLAC__stream_decoder_delete(trans->decoder);

flac_fail:
    id3_tag_delete(trans->id3tag);

id3_fail:
    free(trans->buffer.data);
    free(trans);

trans_fail:
    return NULL;
}

/* Read some bytes into the internal buffer and into the given buffer. */

int transcoder_read(struct transcoder* trans, char* buff, int offset, int len) {
    /* Client asked for more data than exists. */
    if (offset > trans->totalsize) {
        return 0;
    }
    if (offset+len > trans->totalsize) {
        len = trans->totalsize - offset;
    }

    /*
     * this is an optimisation to speed up the case where applications
     * read the last block first looking for an id3v1 tag (last 128
     * bytes). If we detect this case, we give back the id3v1 tag
     * prepended with zeros to fill the block
     */
    if (offset > trans->buffer.pos
        && offset + len > (trans->totalsize - 128)) {
        int id3start = trans->totalsize - 128;

        // zero the buffer
        memset(buff, 0, len);

        if (id3start >= offset) {
            memcpy(buff + (id3start-offset), trans->id3v1tag,
                   len - (id3start-offset));
        } else {
            memcpy(buff, trans->id3v1tag+(128-len), len);
        }

        return len;
    }

    if (trans->decoder && trans->encoder) {
        // transcode up to what we need if possible
        while (trans->buffer.pos < offset + len) {
            if (FLAC__stream_decoder_get_state(trans->decoder)
                < FLAC__STREAM_DECODER_END_OF_STREAM) {
                FLAC__stream_decoder_process_single(trans->decoder);
            } else {
                transcoder_finish(trans);
                break;
            }
        }
    }

    // truncate if we didnt actually get len
    if (trans->buffer.pos < offset + len) {
        len = trans->buffer.pos - offset;
        if (len < 0) len = 0;
    }

    memcpy(buff, trans->buffer.data+offset, len);
    return len;
}

/* Close the input file and free everything but the initial buffer. */

int transcoder_finish(struct transcoder* trans) {
    int len = 0;
    uint8_t* write_ptr;

    // flac cleanup
    if (trans->decoder) {
        FLAC__stream_decoder_finish(trans->decoder);
        FLAC__stream_decoder_delete(trans->decoder);
        trans->decoder = NULL;
    }

    // lame cleanup
    if (trans->encoder) {
        write_ptr = buffer_write_prepare(&trans->buffer, BUFSIZE);
        if (write_ptr) {
            len = lame_encode_flush(trans->encoder, write_ptr, BUFSIZE);
            if (len >= 0) {
                trans->buffer.pos += len;
            }
        }
        lame_close(trans->encoder);
        trans->encoder = NULL;

        /* Write the ID3v1 tag, always 128 bytes from end. */
        mp3fs_debug("Finishing file. Correct size: %lu, actual size: %lu",
                    trans->buffer.pos + 128, trans->totalsize);
        trans->buffer.pos = trans->totalsize - 128;
        buffer_write(&trans->buffer, trans->id3v1tag, 128);
        len += 128;
    }

    return len;
}

/* Free the transcoder structure. */

void transcoder_delete(struct transcoder* trans) {
    transcoder_finish(trans);
    free(trans->buffer.data);
    free(trans);
}
