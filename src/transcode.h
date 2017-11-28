/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
 * Copyright (C) 2017 FFmpeg supplementals by Norbert Schlia (nschlia@oblivion-software.de)
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

#ifndef TRANSCODE_H
#define TRANSCODE_H

#pragma once

// For release 1.0: disable ISMV (smooth streaming) and image filters
#define DISABLE_ISMV
#define DISABLE_AVFILTER

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <syslog.h>
#include <stdarg.h>

/* Global program parameters */
extern struct mp3fs_params {
    // Paths
    const char *    basepath;
    const char *    mountpath;
    // Output type
    const char*     desttype;
#ifndef DISABLE_ISMV
    int             enable_ismv;    // TODO: produces rediculuosly large files
#endif
    // Audio
    unsigned int    audiobitrate;
    unsigned int    audiosamplerate;
    // Video
    unsigned int    videobitrate;
#ifndef DISABLE_AVFILTER
    unsigned int    videowidth;     // TODO
    unsigned int    videoheight;    // TODO
    int             deinterlace;    // TODO
#endif
    // mp3fs options
    int             debug;
    const char*     log_maxlevel;
    int             log_stderr;
    int             log_syslog;
    const char*     logfile;
    // Background recoding/caching
    time_t          expiry_time;                // TODO: Time (seconds) after which an cache entry is deleted
    time_t          max_inactive_suspend;       // Time (seconds) that must elapse without access until transcoding is suspened
    time_t          max_inactive_abort;         // Time (seconds) that must elapse without access until transcoding is aborted
    int             max_cache_size;             // TODO: Max. cache size in MB. When exceeded, oldest entries will be pruned
    int             max_threads;                // Max. number of recoder threads
    const char*     cachepath;                  // Disk cache path, defaults to /tmp
} params;

/* Fuse operations struct */
extern struct fuse_operations mp3fs_ops;

/*
 * Forward declare transcoder struct. Don't actually define it here, to avoid
 * including coders.h and turning into C++.
 */
struct Cache_Entry;

/* Define lists of available encoder and decoder extensions. */
extern const char* encoder_list[];
extern const char* decoder_list[];

#ifdef __cplusplus
extern "C" {
#endif

void cache_path(char *dir, size_t size);
void cache_new(void);
void cache_delete(void);

// Simply get encoded file size (do not create the whole encoder/decoder objects)
int transcoder_cached_filesize(const char *filename, struct stat *stbuf);

/* Functions for doing transcoding, called by main program body */
struct Cache_Entry* transcoder_new(const char *filename, int begin_transcode);
ssize_t transcoder_read(struct Cache_Entry* cache_entry, char* buff, off_t offset,
                        size_t len);
//int transcoder_finish(struct Cache_Entry* cache_entry);
void transcoder_delete(struct Cache_Entry* cache_entry);
size_t transcoder_get_size(struct Cache_Entry* cache_entry);
size_t transcoder_buffer_watermark(struct Cache_Entry* cache_entry);
size_t transcoder_buffer_tell(struct Cache_Entry* cache_entry);
void transcoder_exit(void);

/* Check for availability of audio types. */
int check_encoder(const char* type);
int check_decoder(const char* type);

/* Functions to print output until C++ conversion is done. */
void mp3fs_trace(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void mp3fs_debug(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void mp3fs_warning(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void mp3fs_info(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void mp3fs_error(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
#ifndef USING_LIBAV
void ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl);
#endif

int init_logging(const char* logfile, const char* max_level, int to_stderr, int to_syslog);

#ifdef __cplusplus
}
#endif

#endif
