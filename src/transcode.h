/*
 * FileTranscoder interface for ffmpegfs
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
 * Copyright (C) 2017 FFmpeg support by Norbert Schlia (nschlia@oblivion-software.de)
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
#include <stdarg.h>

/* Global program parameters */
extern struct ffmpegfs_params
{
    // Paths
    const char *    m_basepath;
    const char *    m_mountpath;
    // Output type
    const char*     m_desttype;
#ifndef DISABLE_ISMV
    int             m_enable_ismv;              // TODO Bug #2240: produces ridiculously large files
#endif
    // Audio
    unsigned int    m_audiobitrate;
    unsigned int    m_audiosamplerate;
    // Video
    unsigned int    m_videobitrate;
#ifndef DISABLE_AVFILTER
    unsigned int    m_videowidth;               // TODO Feature #2243: set video width
    unsigned int    m_videoheight;              // TODO Feature #2243: set video height
    int             m_deinterlace;              // TODO Feature #2227: deinterlace video
#endif
    // ffmpegfs options
    int             m_debug;
    const char*     m_log_maxlevel;
    int             m_log_stderr;
    int             m_log_syslog;
    const char*     m_logfile;
    // Background recoding/caching
    time_t          m_expiry_time;              // Time (seconds) after which an cache entry is deleted
    time_t          m_max_inactive_suspend;     // Time (seconds) that must elapse without access until transcoding is suspended
    time_t          m_max_inactive_abort;       // Time (seconds) that must elapse without access until transcoding is aborted
    size_t          m_max_cache_size;           // Max. cache size in MB. When exceeded, oldest entries will be pruned
    size_t          m_min_diskspace;            // Min. diskspace required for cache
    const char*     m_cachepath;                // Disk cache path, defaults to /tmp
    int             m_disable_cache;            // Disable cache
    time_t          m_cache_maintenance;        // Prune timer interval
    int             m_prune_cache;              // Prune cache immediately
    unsigned int    m_max_threads;              // Max. number of recoder threads
} params;

/* Fuse operations struct */
extern struct fuse_operations ffmpegfs_ops;

/*
 * Forward declare transcoder struct. Don't actually define it here, to avoid
 * including coders.h and turning into C++.
 */
struct Cache_Entry;

#ifdef __cplusplus
extern "C" {
#endif

void cache_path(char *dir, size_t size);
int cache_new(void);
void cache_delete(void);

// Simply get encoded file size (do not create the whole encoder/decoder objects)
int transcoder_cached_filesize(const char *filename, struct stat *stbuf);

/* Functions for doing transcoding, called by main program body */
struct Cache_Entry* transcoder_new(const char *filename, int begin_transcode);
ssize_t transcoder_read(struct Cache_Entry* cache_entry, char* buff, off_t offset, size_t len);
void transcoder_delete(struct Cache_Entry* cache_entry);
size_t transcoder_get_size(struct Cache_Entry* cache_entry);
size_t transcoder_buffer_watermark(struct Cache_Entry* cache_entry);
size_t transcoder_buffer_tell(struct Cache_Entry* cache_entry);
void transcoder_exit(void);
int transcoder_cache_maintenance(void);

/* Functions to print output until C++ conversion is done. */
void ffmpegfs_trace(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void ffmpegfs_debug(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void ffmpegfs_warning(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void ffmpegfs_info(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
void ffmpegfs_error(const char* f, ...) __attribute__ ((format(printf, 1, 2)));
#ifndef USING_LIBAV
void ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl);
#endif

int init_logging(const char* logfile, const char* max_level, int to_stderr, int to_syslog);

#ifdef __cplusplus
}
#endif

#endif
