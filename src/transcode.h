/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 Kristofer Henriksson
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
#include <syslog.h>

/* Global program parameters */
extern struct mp3fs_params {
    const char *basepath;
    unsigned int bitrate;
    int vbr;
    unsigned int quality;
    int debug;
    int gainmode;
    float gainref;
    const char* desttype;
} params;

/* Fuse operations struct */
extern struct fuse_operations mp3fs_ops;

#define mp3fs_debug(f, ...) syslog(LOG_DEBUG, f, ## __VA_ARGS__)
#define mp3fs_info(f, ...) syslog(LOG_INFO, f, ## __VA_ARGS__)
#define mp3fs_error(f, ...) syslog(LOG_ERR, f, ## __VA_ARGS__)

/*
 * Forward declare transcoder struct. Don't actually define it here, to avoid
 * including coders.h and turning into C++.
 */
struct transcoder;

/* Define lists of available encoder and decoder extensions. */
extern const char* encoder_list[];
extern const size_t sizeof_encoder_list;
extern const char* decoder_list[];
extern const size_t sizeof_decoder_list;

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

#ifdef __cplusplus
}
#endif
