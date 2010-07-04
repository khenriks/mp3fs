/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) David Collett (daveco@users.sourceforge.net)
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
#include "talloc.h"

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
    FLAC__StreamMetadata_VorbisComment *comment;
    comment = (FLAC__StreamMetadata_VorbisComment *)&metadata->data;
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
    FLAC__StreamMetadata_Picture *picture;
    struct id3_frame *frame;
    id3_ucs4_t       *ucs4;

    picture = (FLAC__StreamMetadata_Picture *)&metadata->data;

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

// lame callback for error/debug/msg
void lame_error(const char *fmt, va_list list) {
    DEBUG(logfd, "LAME error: ");
#ifdef __DEBUG__
    vfprintf(logfd, fmt, list);
#endif
    return;
}

// flac callback for errors
static void error_cb(const FLAC__StreamDecoder *decoder,
                     FLAC__StreamDecoderErrorStatus status,
                     void *client_data) {
    DEBUG(logfd, "FLAC error: %s\n",
          FLAC__StreamDecoderErrorStatusString[status]);
    return;
}

// callbacks for the decoder
static FLAC__StreamDecoderWriteStatus
write_cb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
         const FLAC__int32 *const buffer[], void *client_data) {
    int len, i;
    FileTranscoder trans = (FileTranscoder)client_data;

    if (frame->header.blocksize < 1152) {
        //printf("ERROR: got less than a frame: %d\n", frame->header.blocksize);
    }

    // convert down to shorts
    for (i=0; i<frame->header.blocksize; i++) {
        trans->lbuf[i] = (short int)buffer[0][i];
        // ignore rbuf for mono sources
        if (trans->info.channels > 1) {
            trans->rbuf[i] = (short int)buffer[1][i];
        }
    }

    len = lame_encode_buffer(trans->encoder, trans->lbuf, trans->rbuf,
                             frame->header.blocksize, trans->mp3buf, BUFSIZE);

    CALL(trans->buffer, write, (char *)trans->mp3buf, len);

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void meta_cb(const FLAC__StreamDecoder *decoder,
                    const FLAC__StreamMetadata *metadata, void *client_data) {
    char *tmpstr;
    float dbgain;
    FileTranscoder trans = (FileTranscoder)client_data;

    switch (metadata->type) {
        case FLAC__METADATA_TYPE_STREAMINFO:
            memcpy(&trans->info, &metadata->data,
                   sizeof(FLAC__StreamMetadata_StreamInfo));

            /* set the length in the id3tag */
            tmpstr = talloc_asprintf(trans, "%" PRIu64,
                trans->info.total_samples*1000/trans->info.sample_rate);
            id3_tag_attachframe(trans->id3tag, make_frame("TLEN", tmpstr));
            talloc_free(tmpstr);

// DEBUG(logfd, "%s: sample_rate: %u\nchannels: %u\nbits/sample: %u\ntotal_samples: %u\n",
// trans->name,
// trans->info.sample_rate, trans->info.channels,
// trans->info.bits_per_sample, trans->info.total_samples);

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

            /* set the track/total */
            if (get_tag(metadata, "TRACKNUMBER")) {
                tmpstr = talloc_asprintf(trans, "%s",
                                         get_tag(metadata, "TRACKNUMBER"));
                if (get_tag(metadata, "TRACKTOTAL"))
                    tmpstr = talloc_asprintf_append(tmpstr, "/%s",
                                        get_tag(metadata, "TRACKTOTAL"));
                id3_tag_attachframe(trans->id3tag,
                                    make_frame(ID3_FRAME_TRACK, tmpstr));
                talloc_free(tmpstr);
            }

            /* set the disc/total, also less common */
            if (get_tag(metadata, "DISCNUMBER")) {
                tmpstr = talloc_asprintf(trans, "%s",
                                         get_tag(metadata, "DISCNUMBER"));
                if (get_tag(metadata, "DISCTOTAL"))
                    tmpstr = talloc_asprintf_append(tmpstr, "/%s",
                                        get_tag(metadata, "DISCTOTAL"));
                id3_tag_attachframe(trans->id3tag, make_frame("TPOS", tmpstr));
                talloc_free(tmpstr);
            }

            /*
             * Use the Replay Gain tag to set volume scaling. First check
             * for album gain, then try track gain.
             */
            if (get_tag(metadata, "REPLAYGAIN_ALBUM_GAIN")) {
                dbgain = atof(get_tag(metadata, "REPLAYGAIN_ALBUM_GAIN"));
                if (dbgain)
                    lame_set_scale(trans->encoder, powf(10, dbgain/20));
            } else if (get_tag(metadata, "REPLAYGAIN_TRACK_GAIN")) {
                dbgain = atof(get_tag(metadata, "REPLAYGAIN_TRACK_GAIN"));
                if (dbgain)
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

/*******************************************************************
 FileTranscoder Class implementation
*******************************************************************/

FileTranscoder FileTranscoder_Con(FileTranscoder self, char *filename) {
    self->buffer = CONSTRUCT(StringIO, StringIO, Con, self);
    self->name = talloc_strdup(self, filename);

    self->id3tag = id3_tag_new();
    if (self->id3tag == NULL) {
        goto id3_fail;
    }

    id3_tag_attachframe(self->id3tag, make_frame("TSSE", "MP3FS"));

    // set the original (flac) filename
    self->orig_name = talloc_size(self, strlen(self->name) + 2);
    strncpy(self->orig_name, self->name, strlen(self->name));

    // translate name back to original
    {
        char *ptr = self->orig_name + strlen(self->orig_name) - 1;
        while (ptr > self->orig_name && *ptr != '.') --ptr;
        if (strcmp(ptr, ".mp3") == 0)
            strcpy(ptr, ".flac");
    }

    // create and initialise decoder
    self->decoder = FLAC__stream_decoder_new();
    if (self->decoder == NULL) {
        goto flac_fail;
    }

    FLAC__stream_decoder_set_metadata_respond(self->decoder,
                                    FLAC__METADATA_TYPE_VORBIS_COMMENT);
    FLAC__stream_decoder_set_metadata_respond(self->decoder,
                                              FLAC__METADATA_TYPE_PICTURE);

    if (FLAC__stream_decoder_init_file(self->decoder, self->orig_name,
                                       &write_cb, &meta_cb, &error_cb,
                                       (void *)self) !=
        FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        goto init_flac_fail;
    }

    // process a single block, the first block is always
    // STREAMINFO. This will fill in the info structure which is
    // required to initialise the encoder
    FLAC__stream_decoder_process_single(self->decoder);

    // create encoder
    self->encoder = lame_init();
    if (self->encoder == NULL) {
        goto encoder_fail;
    }
    lame_set_quality(self->encoder, MP3_QUALITY);
    lame_set_brate(self->encoder, bitrate);
    lame_set_bWriteVbrTag(self->encoder, 0);
    lame_set_errorf(self->encoder, &lame_error);
    lame_set_debugf(self->encoder, &lame_error);
    lame_set_msgf(self->encoder, &lame_error);
    lame_set_num_samples(self->encoder, self->info.total_samples);
    lame_set_in_samplerate(self->encoder, self->info.sample_rate);
    lame_set_num_channels(self->encoder, self->info.channels);
  //DEBUG(logfd, "Sample Rate: %d\n", self->info.sample_rate);
  //Maybe there's a better way to see if file isn't really FLAC,
  //this is just to prevent division by zero
    if (!self->info.sample_rate) {
        goto encoder_fail;
    }

    // Now process the rest of the metadata. This will fill in the
    // id3tag.
    FLAC__stream_decoder_process_until_end_of_metadata(self->decoder);

    // now we can initialise the encoder
    if (lame_init_params(self->encoder) == -1) {
        goto init_encoder_fail;
    }

    self->framesize = 144*bitrate*1000/self->info.sample_rate; // 144=1152/8
    self->numframes = divideround(self->info.total_samples, 1152) + 2;

    /* Now we have to render our id3tag so that we know how big the total
     * file is. We write the id3v3 tag directly into the front of the
     * stringio. The id3v1 tag is written into a fixed 128 byte buffer (it
     * is a fixed size)
     */

    /*
     * disable id3 compression because it hardly saves space and some
     * players don't like it
     */
    id3_tag_options(self->id3tag, ID3_TAG_OPTION_COMPRESSION, 0);

    // grow buffer and write v2 tag
    CALL(self->buffer, seek, id3_tag_render(self->id3tag, 0), SEEK_SET);
    id3_tag_render(self->id3tag, (id3_byte_t *)self->buffer->data);

    // store v1 tag
    id3_tag_options(self->id3tag, ID3_TAG_OPTION_ID3V1, ~0);
    id3_tag_render(self->id3tag, (id3_byte_t *)self->id3v1tag);

    // id3v2 + lame stuff + mp3 data + id3v1
    self->totalsize = self->buffer->size
        + divideround((long long)self->numframes*144*bitrate*10,
                      self->info.sample_rate/100) + 128;

    id3_tag_delete(self->id3tag);
    return self;

init_encoder_fail:
    lame_close(self->encoder);

encoder_fail:
init_flac_fail:
    FLAC__stream_decoder_delete(self->decoder);

flac_fail:
    id3_tag_delete(self->id3tag);

id3_fail:
    talloc_free(self);
    return NULL;
}

int FileTranscoder_Finish(FileTranscoder self) {
    int len=0;

    // flac cleanup
    if (self->decoder != NULL) {
        FLAC__stream_decoder_finish(self->decoder);
        FLAC__stream_decoder_delete(self->decoder);
        self->decoder = NULL;
    }

    // lame cleanup
    if (self->encoder != NULL) {
        len = lame_encode_flush(self->encoder, self->mp3buf, BUFSIZE);
        if (len>0)
            CALL(self->buffer, write, (char *)self->mp3buf, len);
        lame_close(self->encoder);
        self->encoder = NULL;

        if (self->buffer->size + 128 != self->totalsize) {
            // write the id3v1 tag, always 128 bytes from end
            DEBUG(logfd, "Something went wrong with file size calculation: "
                  "off by %d\n", self->buffer->size + 128 - self->totalsize);
            CALL(self->buffer, seek, self->totalsize-128, SEEK_SET);
        }
        CALL(self->buffer, write, self->id3v1tag, 128);
        len += 128;
    }

    return len;
}

int FileTranscoder_Read(FileTranscoder self, char *buff, int offset, int len) {
    if (offset+len > self->totalsize) {
        len = self->totalsize - offset;
    }

    /*
     * this is an optimisation to speed up the case where applications
     * read the last block first looking for an id3v1 tag (last 128
     * bytes). If we detect this case, we give back the id3v1 tag
     * prepended with zeros to fill the block
     */
    if (offset > self->buffer->size
        && offset + len > (self->totalsize - 128)) {
        int id3start = self->totalsize - 128;

        // zero the buffer
        memset(buff, 0, len);

        if (id3start >= offset) {
            memcpy(buff + (id3start-offset), self->id3v1tag,
                   len - (id3start-offset));
        } else {
            memcpy(buff, self->id3v1tag+(128-len), len);
        }

        return len;
    }

    if (self->decoder && self->encoder) {
        // transcode up to what we need if possible
        while (self->buffer->size < offset + len) {
            if (FLAC__stream_decoder_get_state(self->decoder)
                < FLAC__STREAM_DECODER_END_OF_STREAM) {
                FLAC__stream_decoder_process_single(self->decoder);
            } else {
                self->Finish(self);
                break;
            }
        }
    }

    // truncate if we didnt actually get len
    if (self->buffer->size < offset + len) {
        len = self->buffer->size - offset;
        if (len < 0) len = 0;
    }

    memcpy(buff, self->buffer->data+offset, len);
    return len;
}

VIRTUAL(FileTranscoder, Object)
    VATTR(readptr) = 0;
    VATTR(id3tag) = NULL;
    VMETHOD(Con) = FileTranscoder_Con;
    VMETHOD(Read) = FileTranscoder_Read;
    VMETHOD(Finish) = FileTranscoder_Finish;
END_VIRTUAL
