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

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#include <lame/lame.h>
#include <id3tag.h>
#include <syslog.h>

#define FLAC_BLOCKSIZE 4608
#define BUFSIZE 2 * FLAC_BLOCKSIZE

/* Global program parameters */
extern struct mp3fs_params {
    const char *basepath;
    unsigned int bitrate;
    unsigned int quality;
    int debug;
    int gainmode;
    float gainref;
} params;

/* Fuse operations struct */
extern struct fuse_operations mp3fs_ops;

#define mp3fs_debug(f, ...) syslog(LOG_DEBUG, f, ## __VA_ARGS__)
#define mp3fs_info(f, ...) syslog(LOG_INFO, f, ## __VA_ARGS__)
#define mp3fs_error(f, ...) syslog(LOG_ERR, f, ## __VA_ARGS__)

/* Internal buffer used for output file */
struct mp3_buffer {
    uint8_t* data;
    unsigned long pos;
    unsigned long size;
};

/* Transcoder parameters for open mp3 */
struct transcoder {
    struct mp3_buffer buffer;
    unsigned long totalsize;

    struct id3_tag *id3tag;
    id3_byte_t id3v1tag[128];

    FLAC__StreamDecoder *decoder;
    lame_global_flags *encoder;

    FLAC__StreamMetadata_StreamInfo info;

    unsigned char mp3buf[BUFSIZE];
};

struct transcoder* transcoder_new(char *flacname);
int transcoder_read(struct transcoder* trans, char* buff, int offset,
                    int len);
int transcoder_finish(struct transcoder* trans);
void transcoder_delete(struct transcoder* trans);
