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

#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>
#include <lame/lame.h>
#include <id3tag.h>
#include <syslog.h>

#include "class.h"
#include "stringio.h"

#define FLAC_BLOCKSIZE 4608
#define BUFSIZE 2 * FLAC_BLOCKSIZE

/* Global program parameters */
struct mp3fs_params {
    const char      *basepath;
    int             bitrate;
    int             quality;
    int             debug;
};

#define mp3fs_debug(f, ...) syslog(LOG_DEBUG, f, ## __VA_ARGS__)
#define mp3fs_info(f, ...) syslog(LOG_INFO, f, ## __VA_ARGS__)
#define mp3fs_error(f, ...) syslog(LOG_ERR, f, ## __VA_ARGS__)

CLASS(FileTranscoder, Object)
    StringIO buffer;
    int readptr;
    int framesize;
    int numframes;
    int totalsize;
    char *name;
    char *orig_name;
    struct id3_tag *id3tag;
    char id3v1tag[128];

    lame_global_flags *encoder;
    FLAC__StreamDecoder *decoder;
    FLAC__StreamMetadata_StreamInfo info;
    short int lbuf[FLAC_BLOCKSIZE];
    short int rbuf[FLAC_BLOCKSIZE];
    unsigned char mp3buf[BUFSIZE];

    FileTranscoder METHOD(FileTranscoder, Con, char *filename);
    int METHOD(FileTranscoder, Read, char *buff, int offset, int len);
    int METHOD(FileTranscoder, Finish);

END_CLASS
