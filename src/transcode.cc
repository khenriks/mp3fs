/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
 * FFMPEG supplementals (c) 2017 by Norbert Schlia (nschlia@oblivon-software.de)
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

#include "transcode.h"
#include "ffmpeg_transcoder.h"
#include "buffer.h"
#include "cache.h"
#include "logging.h"
#include "stats_cache.h"
#include "cache_entry.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <unistd.h>

static Cache *cache;
static volatile bool thread_exit;

static void *decoder_thread(void *arg);

namespace {

StatsCache stats_cache;

/*
 * Transcode the buffer until the buffer has enough or until an error occurs.
 * The buffer needs at least 'end' bytes before transcoding stops. Returns true
 * if no errors and false otherwise.
 */
static bool transcode_until(struct Cache_Entry* cache_entry, off_t offset, size_t len) {
    size_t end = offset + len;
    bool success = true;

    if (cache_entry->m_info.m_finished || cache_entry->m_buffer->tell() >= end)
    {
        return true;
    }

    // Wait until decoder thread has reached the desired position
    if (cache_entry->m_is_decoding)
    {
        while (!cache_entry->m_info.m_finished && !cache_entry->m_info.m_error && cache_entry->m_buffer->tell() < end) {
            sleep(0);
        }
        success = !cache_entry->m_info.m_error;
    }
    return success;
}

}

/* Use "C" linkage to allow access from C code. */
extern "C" {

void cache_new(void)
{
    if (cache == NULL)
    {
        mp3fs_debug("Creating media file cache.");
        cache = new Cache;
    }
}

void cache_delete(void)
{
    Cache *p = cache;
    cache = NULL;
    if ( p!= NULL)
    {
        mp3fs_debug("Deleting media file cache.");
        delete p;
    }
}

int transcoder_cached_filesize(const char* filename, struct stat *stbuf) {
    mp3fs_debug("Retrieving encoded size for %s.", filename);

    size_t encoded_filesize;
    if (stats_cache.get_filesize(filename, stbuf->st_mtime, encoded_filesize)) {
        stbuf->st_size = encoded_filesize;
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
        return true;
    }
    else {
        return false;
    }
}

/* Allocate and initialize the transcoder */

struct Cache_Entry* transcoder_new(const char* filename, int begin_transcode) {
    mp3fs_debug("Creating transcoder object for %s.", filename);

    /* Allocate transcoder structure */
    struct Cache_Entry* cache_entry = cache->open(filename);
    if (!cache_entry) {
        goto trans_fail;
    }

    if (!cache_entry->open(begin_transcode))
    {
        goto init_fail;
    }

    if (!cache_entry->m_is_decoding && !cache_entry->m_info.m_finished)
    {
        /* Create transcoder object. */

        mp3fs_debug("Ready to initialise decoder.");

        if (cache_entry->m_transcoder->open_file(filename) < 0) {
            goto init_fail;
        }

        mp3fs_debug("Transcoder initialised successfully.");
        //        if (params.statcachesize > 0 && cache_entry->m_encoded_filesize != 0) {
        //            stats_cache.put_filesize(cache_entry->m_filename, cache_entry->mtime(), cache_entry->m_encoded_filesize);
        //        }

        stats_cache.get_filesize(cache_entry->m_filename, cache_entry->mtime(), cache_entry->m_info.m_encoded_filesize);

        if (begin_transcode)
        {
            pthread_attr_t attr;
            //            int stack_size;
            int s;

            //            if (!cache_entry->open())
            //            {
            //                goto init_fail;
            //            }

            if (!cache_entry->m_info.m_finished)
            {
                // Must decode the file, otherwise simply use cache
                cache_entry->m_is_decoding = true;

                /* Initialize thread creation attributes */

                s = pthread_attr_init(&attr);
                if (s != 0)
                {
                    mp3fs_error("Error creating thread attributes: %s", strerror(s));
                    goto init_fail;
                }

                //            if (stack_size > 0) {
                //                s = pthread_attr_setstacksize(&attr, stack_size);
                //                if (s != 0)
                //                {
                //                    mp3fs_error("Error setting stack size: %s", strerror(s));
                //                    pthread_attr_destroy(&attr);
                //                    goto init_fail;
                //                }
                //            }

                s = pthread_create(&cache_entry->m_thread_id, &attr, &decoder_thread, cache_entry);
                if (s != 0)
                {
                    mp3fs_error("Error creating thread: %s", strerror(s));
                    pthread_attr_destroy(&attr);
                    goto init_fail;
                }

                /* Destroy the thread attributes object, since it is no longer needed */

                s = pthread_attr_destroy(&attr);
                if (s != 0)
                {
                    mp3fs_warning("Error destroying thread attributes: %s", strerror(s));
                }
            }
        }
    }
    //    else
    //    {
    //        if (!cache_entry->open())
    //        {
    //            goto init_fail;
    //        }
    //    }
    return cache_entry;

init_fail:
    cache_entry->m_is_decoding = false;
    cache->close(&cache_entry);

trans_fail:
    return NULL;
}

/* Read some bytes into the internal buffer and into the given buffer. */

ssize_t transcoder_read(struct Cache_Entry* cache_entry, char* buff, off_t offset, size_t len) {
    mp3fs_debug("Reading %zu bytes from offset %jd.", len, (intmax_t)offset);

    //    if ((size_t)offset > transcoder_get_size(cache_entry)) {
    //        return -1;
    //    }
    //    if (offset + len > transcoder_get_size(cache_entry)) {
    //        len = transcoder_get_size(cache_entry) - offset;
    //    }

    // TODO: Avoid favoring MP3 in program structure.
    /*
     * If we are encoding to MP3 and the requested data overlaps the ID3v1 tag
     * at the end of the file, do not encode data first up to that position.
     * This optimizes the case where applications read the end of the file
     * first to read the ID3v1 tag.
     */

    if (strcmp(params.desttype, "mp3") == 0 &&
            (size_t)offset > cache_entry->m_buffer->tell()
            && offset + len >
            (transcoder_get_size(cache_entry) - ID3V1_TAG_LENGTH)) {

        memcpy(buff, cache_entry->id3v1tag(), ID3V1_TAG_LENGTH);

        return len;
    }

    // Set last access time
    cache_entry->m_info.m_access_time = time(NULL);

    bool success = transcode_until(cache_entry, offset, len);

    if (!success) {
        return -1;
    }

    // truncate if we didn't actually get len
    if (cache_entry->m_buffer->tell() < (size_t) offset) {
        len = 0;
    } else if (cache_entry->m_buffer->tell() < offset + len) {
        len = cache_entry->m_buffer->tell() - offset;
    }

    cache_entry->m_buffer->copy((uint8_t*)buff, offset, len);

    return len;
}

/* Close the input file and free everything but the initial buffer. */

static int transcoder_finish(struct Cache_Entry* cache_entry) {
    // decoder cleanup
    time_t decoded_file_mtime = 0;

    decoded_file_mtime = cache_entry->mtime();

    // encoder cleanup
    int len = cache_entry->m_transcoder->encode_finish();
    if (len == -1) {
        return -1;
    }

    /* Check encoded buffer size. */
    cache_entry->m_info.m_encoded_filesize = cache_entry->m_buffer->buffer_watermark();
    cache_entry->m_info.m_finished = true;
    cache_entry->m_is_decoding = false;

    if (!cache_entry->m_buffer->reserve(cache_entry->m_info.m_encoded_filesize)) {
        mp3fs_warning("FFMPEG transcoder: Unable to truncate buffer.");
    }

    mp3fs_debug("Finishing file. Predicted size: %zu, final size: %zu, diff: %zi.", cache_entry->calculate_size(), cache_entry->m_info.m_encoded_filesize, cache_entry->m_info.m_encoded_filesize - cache_entry->calculate_size());

    if (params.statcachesize > 0 && cache_entry->m_info.m_encoded_filesize != 0) {
        stats_cache.put_filesize(cache_entry->m_filename, decoded_file_mtime, cache_entry->m_info.m_encoded_filesize);
    }

    cache_entry->flush();

    return 0;
}

/* Free the transcoder structure. */

void transcoder_delete(struct Cache_Entry* cache_entry) {
    cache->close(&cache_entry);
}

/* Return size of output file, as computed by Encoder. */
size_t transcoder_get_size(struct Cache_Entry* cache_entry) {
    if (cache_entry->m_info.m_encoded_filesize != 0) {
        return cache_entry->m_info.m_encoded_filesize;
    } else
        return cache_entry->calculate_size();
}
}

size_t transcoder_buffer_watermark(struct Cache_Entry* cache_entry) {
    return cache_entry->m_buffer->buffer_watermark();
}

size_t transcoder_buffer_tell(struct Cache_Entry* cache_entry) {
    return cache_entry->m_buffer->tell();
}

void transcoder_exit(void) {
    thread_exit = true;
}

static void *decoder_thread(void *arg)
{
    Cache_Entry *cache_entry = (Cache_Entry *)arg;
    bool timeout = false;
    bool success = true;

    try
    {
        if (!cache_entry->open())
        {
            throw false;
        }

        if (cache_entry->m_transcoder->open_out_file(cache_entry->m_buffer) == -1) {
            throw false;
        }

        mp3fs_debug("Output file opened. Beginning transcoding.");

        while (!cache_entry->m_info.m_finished && !(timeout = cache_entry->decode_timeout()) && !thread_exit) {
            int stat = cache_entry->m_transcoder->process_single_fr();

            if (stat == -1 || (stat == 1 && transcoder_finish(cache_entry) == -1)) {
                errno = EIO;
                success = false;
                break;
            }
        }
    }
    catch (bool _success)
    {
        success = _success;
    }

    if (timeout || thread_exit)
    {
        cache_entry->m_is_decoding = false;
        cache_entry->m_info.m_finished = false;
        cache_entry->m_info.m_error = true;

        if (timeout) {
            mp3fs_debug("Timeout! Transcoding aborted for file '%s'.", cache_entry->m_filename.c_str());
            cache->close(&cache_entry, true);  // After timeout need to delete this here
        }
        else {
            mp3fs_debug("Thread exit! Transcoding aborted for file '%s'.", cache_entry->m_filename.c_str());
        }
    }
    else
    {
        cache_entry->m_info.m_error = !success;

        mp3fs_debug("Transcoding complete.");
    }
    cache_entry->close();

    return NULL;
}

void mp3fs_trace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(TRACE, format, args);
    va_end(args);
}

void mp3fs_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(DEBUG, format, args);
    va_end(args);
}

void mp3fs_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(INFO, format, args);
    va_end(args);
}

void mp3fs_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(WARNING, format, args);
    va_end(args);
}

void mp3fs_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(ERROR, format, args);
    va_end(args);
}

#ifndef USING_LIBAV
void ffmpeg_log(void *ptr, int level, const char *fmt, va_list vl)
{
    va_list vl2;
    char line[1024];
    Logging::level mp3fs_level = ERROR;
    static int print_prefix = 1;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

    // Map log level
    // AV_LOG_PANIC     0
    // AV_LOG_FATAL     8
    // AV_LOG_ERROR    16
    if (level <= AV_LOG_ERROR)
    {
        mp3fs_level = ERROR;
    }
    // AV_LOG_WARNING  24
    else if (level <= AV_LOG_WARNING)
    {
        mp3fs_level = WARNING;
    }
#ifdef AV_LOG_TRACE
    // AV_LOG_INFO     32
    else if (level <= AV_LOG_INFO)
    {
        mp3fs_level = DEBUG;
    }
    // AV_LOG_VERBOSE  40
    // AV_LOG_DEBUG    48
    // AV_LOG_TRACE    56
    else // if (level <= AV_LOG_TRACE)
    {
        mp3fs_level = TRACE;
    }
#else
    // AV_LOG_INFO     32
    else // if (level <= AV_LOG_INFO)
    {
        mp3fs_level = DEBUG;
    }
#endif

    log_with_level(mp3fs_level, "", line);
}
#endif

int init_logging(const char* logfile, const char* max_level, int to_stderr,
                 int to_syslog) {
    static const std::map<std::string, Logging::level> level_map = {
        {"DEBUG", DEBUG},
        {"WARNING", WARNING},
        {"INFO", INFO},
        {"ERROR", ERROR},
    };

    auto it = level_map.find(max_level);

    if (it == level_map.end()) {
        fprintf(stderr, "Invalid logging level string: %s\n", max_level);
        return false;
    }

    return InitLogging(logfile, it->second, to_stderr, to_syslog);
}
