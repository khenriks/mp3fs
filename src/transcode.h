/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
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

#ifndef MP3FS_TRANSCODE_H
#define MP3FS_TRANSCODE_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <syslog.h>

/* Global program parameters */
extern struct mp3fs_params {
    const char *basepath;
    unsigned int bitrate;
    int debug;
    const char* desttype;
    int gainmode;
    float gainref;
    const char* log_maxlevel;
    int log_stderr;
    int log_syslog;
    const char* logfile;
    unsigned int quality;
    unsigned int statcachesize;
    int vbr;
} params;

/* Fuse operations struct */
extern struct fuse_operations mp3fs_ops;

/*
 * Forward declare transcoder struct. Don't actually define it here, to avoid
 * including coders.h and turning into C++.
 */
struct transcoder;

/* Define lists of available encoder and decoder extensions. */
extern const char* encoder_list[];
extern const size_t encoder_list_len;
extern const char* decoder_list[];
extern const size_t decoder_list_len;

#ifdef __cplusplus
extern "C" {
#endif

/* Functions for doing transcoding, called by main program body */
struct transcoder* transcoder_new(char* filename);
ssize_t transcoder_read(struct transcoder* trans, char* buff, off_t offset,
                        size_t len);
int transcoder_finish(struct transcoder* trans);
void transcoder_delete(struct transcoder* trans);
size_t transcoder_get_size(struct transcoder* trans);

/* Check for availability of audio types. */
int check_encoder(const char* type);
int check_decoder(const char* type);

/* Print codec versions. */
void print_codec_versions();

/* Functions to print output until C++ conversion is done. */
void mp3fs_debug(const char* f, ...) __attribute__ ((format(printf, 1, 2)));;
void mp3fs_error(const char* f, ...) __attribute__ ((format(printf, 1, 2)));;

int init_logging(const char* logfile, const char* max_level, int to_stderr,
                 int to_syslog);

#ifdef __cplusplus
}
#endif

#endif  // MP3FS_TRANSCODE_H
