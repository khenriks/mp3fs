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

#include "transcode.h"
#include "ffmpeg_transcoder.h"
#include "buffer.h"
#include "cache.h"
#include "logging.h"
#include "cache_entry.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <assert.h>

typedef struct tagThread_Data {
    pthread_mutex_t  m_mutex;
    pthread_cond_t   m_cond;
    bool             m_initialised;
    void *           m_arg;
} Thread_Data;

static Cache *cache;
static volatile bool thread_exit;
static volatile int thread_count;

static void *decoder_thread(void *arg);
static int transcode_finish(struct Cache_Entry* cache_entry);

/*
 * Transcode the buffer until the buffer has enough or until an error occurs.
 * The buffer needs at least 'end' bytes before transcoding stops. Returns true
 * if no errors and false otherwise.
 */
static bool transcode_until(struct Cache_Entry* cache_entry, off_t offset, size_t len) {
    size_t end = offset + len;
    bool success = true;

    if (cache_entry->m_cache_info.m_finished || cache_entry->m_buffer->tell() >= end)
    {
        return true;
    }

    // Wait until decoder thread has reached the desired position
    if (cache_entry->m_is_decoding)
    {
        while (!cache_entry->m_cache_info.m_finished && !cache_entry->m_cache_info.m_error && cache_entry->m_buffer->tell() < end) {
            sleep(0);
        }
        success = !cache_entry->m_cache_info.m_error;
    }
    return success;
}

/* Close the input file and free everything but the initial buffer. */

static int transcode_finish(struct Cache_Entry* cache_entry) {
    // decoder cleanup
    // encoder cleanup
    int len = cache_entry->m_transcoder->encode_finish();
    if (len == -1) {
        return -1;
    }

    /* Check encoded buffer size. */
    cache_entry->m_cache_info.m_encoded_filesize = cache_entry->m_buffer->buffer_watermark();
    cache_entry->m_cache_info.m_finished = true;
    cache_entry->m_is_decoding = false;

    if (!cache_entry->m_buffer->reserve(cache_entry->m_cache_info.m_encoded_filesize)) {
        mp3fs_warning("Unable to truncate buffer.");
    }

    mp3fs_debug("Finishing file. Predicted size: %zu, final size: %zu, diff: %zi.", cache_entry->calculate_size(), cache_entry->m_cache_info.m_encoded_filesize, cache_entry->m_cache_info.m_encoded_filesize - cache_entry->calculate_size());

    cache_entry->flush();

    return 0;
}

/* Use "C" linkage to allow access from C code. */
extern "C" {

void cache_path(char *dir, size_t size)
{
    if (params.cachepath != NULL)
    {
        *dir = 0;
        strncat(dir, params.cachepath, size - 1);
    }
    else
    {
        tempdir(dir, size);
    }

    if (*dir && *(dir + strlen(dir)) != '/')
    {
        strncat(dir, "/", size - 1);
    }
    strncat(dir, "mp3fs", size - 1);
}

int cache_new(void)
{
    if (cache == NULL)
    {
        mp3fs_debug("Creating media file cache.");
        cache = new Cache;
        if (cache == NULL)
        {
            mp3fs_error("ERROR: creating media file cache. Out of memory.");
            fprintf(stderr, "ERROR: creating media file cache. Out of memory.\n");
            return -1;
        }

        if (!cache->load_index())
        {
            fprintf(stderr, "ERROR: creating media file cache.\n");
            return -1;
        }
    }
    return 0;
}

void cache_delete(void)
{
    Cache *p = cache;
    cache = NULL;
    if (p != NULL)
    {
        mp3fs_debug("Deleting media file cache.");
        delete p;
    }
}

int transcoder_cached_filesize(const char* filename, struct stat *stbuf) {
    mp3fs_debug("Retrieving encoded size for '%s'.", filename);

    Cache_Entry* cache_entry = cache->open(filename);
    if (!cache_entry) {
        return false;
    }

    size_t encoded_filesize = cache_entry->m_cache_info.m_encoded_filesize;

    if (encoded_filesize)
    {
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
    Cache_Entry* cache_entry = cache->open(filename);
    if (!cache_entry) {
        return NULL;
    }

    cache_entry->lock();

    try
    {
        if (!cache_entry->open(begin_transcode))
        {
            throw false;
        }

        if (!cache_entry->m_is_decoding && !cache_entry->m_cache_info.m_finished)
        {
            //            if (params.max_threads && thread_count >= params.max_threads)
            //            {
            //                mp3fs_debug("Too many active threads. Unable to start new thread.");

            //                while (!thread_exit && thread_count >= params.max_threads)
            //                {
            //                    sleep(0);
            //                }

            //                if (thread_count >= params.max_threads)
            //                {
            //                    mp3fs_error("Unable to start new thread. Cancelling transcode.");
            //                    fprintf(stderr, "Unable to start new thread. Cancelling transcode."); fflush(stderr);
            //                    errno = EBUSY; // Report resource busy
            //                    throw false;
            //                }
            //            }

            /* Create transcoder object. */
			
            mp3fs_debug("Ready to initialise decoder.");

            if (cache_entry->m_transcoder->open_input_file(filename) < 0) {
                throw false;
            }

            mp3fs_debug("Transcoder initialised successfully.");

            if (begin_transcode && !cache_entry->m_cache_info.m_finished)
            {
                pthread_attr_t attr;
                // int stack_size;
                int s;

                if (cache_entry->m_cache_info.m_error)
                {
                    // If error occurred last time, clear cache
                    cache_entry->clear();
                }

                // Must decode the file, otherwise simply use cache
                cache_entry->m_is_decoding = true;

                /* Initialize thread creation attributes */
                s = pthread_attr_init(&attr);
                if (s != 0)
                {
                    mp3fs_error("Error creating thread attributes: %s", strerror(s));
                    throw false;
                }

                //if (stack_size > 0) {
                //  s = pthread_attr_setstacksize(&attr, stack_size);
                //  if (s != 0)
                //  {
                //      mp3fs_error("Error setting stack size: %s", strerror(s));
                //      pthread_attr_destroy(&attr);
                //      throw false;
                //  }
                //}

                Thread_Data* thread_data = (Thread_Data*)malloc(sizeof(Thread_Data));

                thread_data->m_initialised = false;
                thread_data->m_arg = cache_entry;

                pthread_mutex_init(&thread_data->m_mutex, 0);
                pthread_cond_init (&thread_data->m_cond , 0);

                pthread_mutex_lock(&thread_data->m_mutex);
                s = pthread_create(&cache_entry->m_thread_id, &attr, &decoder_thread, thread_data);
                if (s == 0)
                {
                    pthread_cond_wait(&thread_data->m_cond, &thread_data->m_mutex);
                }
                pthread_mutex_unlock(&thread_data->m_mutex);

                free(thread_data); // can safely be done here, will not be used in thread from now on

                if (s != 0)
                {
                    mp3fs_error("Error creating thread: %s", strerror(s));
                    pthread_attr_destroy(&attr);
                    throw false;
                }

                /* Destroy the thread attributes object, since it is no longer needed */

                s = pthread_attr_destroy(&attr);
                if (s != 0)
                {
                    mp3fs_warning("Error destroying thread attributes: %s", strerror(s));
                }
            }
        }
        else
        {
            // Store access time
            cache_entry->update_access();
        }

        cache_entry->unlock();
    }
    catch (bool /*_bSuccess*/)
    {
        cache_entry->m_is_decoding = false;
        cache_entry->unlock();
        cache->close(&cache_entry);
    }

    return cache_entry;
}

/* Read some bytes into the internal buffer and into the given buffer. */

ssize_t transcoder_read(struct Cache_Entry* cache_entry, char* buff, off_t offset, size_t len) {
    mp3fs_trace("Reading %zu bytes from offset %jd.", len, (intmax_t)offset);

    cache_entry->lock();

    // Store access time
    cache_entry->update_access();

    try
    {
        /*
     * If we are encoding to MP3 and the requested data overlaps the ID3v1 tag
     * at the end of the file, do not encode data first up to that position.
     * This optimizes the case where applications read the end of the file
     * first to read the ID3v1 tag.
     */
        if (strcmp(params.desttype, "mp3") == 0 &&
                (size_t)offset > cache_entry->m_buffer->tell()
                && offset + len >
                (transcoder_get_size(cache_entry) - ID3V1_TAG_LENGTH))
        {

            memcpy(buff, cache_entry->id3v1tag(), ID3V1_TAG_LENGTH);

            throw len;
        }

        // Set last access time
        cache_entry->m_cache_info.m_access_time = time(NULL);

        bool success = transcode_until(cache_entry, offset, len);

        if (!success)
        {
            throw -1;
        }

        // truncate if we didn't actually get len
        if (cache_entry->m_buffer->tell() < (size_t) offset)
        {
            len = 0;
        }
        else if (cache_entry->m_buffer->tell() < offset + len)
        {
            len = cache_entry->m_buffer->tell() - offset;
        }

        if (!cache_entry->m_buffer->copy((uint8_t*)buff, offset, len))
        {
            len = 0;
            //    throw -1;
        }
        errno = 0;
    }
    catch (int _len)
    {
        len = _len;
    }

    cache_entry->unlock();

    return len;
}

/* Free the transcoder structure. */

void transcoder_delete(struct Cache_Entry* cache_entry) {

    if (!cache_entry->m_is_decoding && cache_entry->m_transcoder->is_open())
    {
        cache_entry->m_transcoder->close();
    }

    cache->close(&cache_entry);
}

/* Return size of output file, as computed by Encoder. */
size_t transcoder_get_size(struct Cache_Entry* cache_entry) {
    if (cache_entry->m_cache_info.m_encoded_filesize != 0) {
        return cache_entry->m_cache_info.m_encoded_filesize;
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
    Thread_Data *thread_data = (Thread_Data*)arg;
    Cache_Entry *cache_entry = (Cache_Entry *)thread_data->m_arg;
    bool timeout = false;
    bool success = true;

    thread_count++;

    try
    {
        if (!cache_entry->open())
        {
            throw false;
        }

        if (cache_entry->m_transcoder->open_output_file(cache_entry->m_buffer) == -1)
        {
            throw false;
        }

        thread_data->m_initialised = true;
        pthread_cond_signal(&thread_data->m_cond);  // signal that we are running

        mp3fs_debug("Output file opened. Beginning transcoding for file '%s'.", cache_entry->filename().c_str());

        while (!cache_entry->m_cache_info.m_finished && !(timeout = cache_entry->decode_timeout()) && !thread_exit) {
            int stat = cache_entry->m_transcoder->process_single_fr();

            if (stat == -1 || (stat == 1 && transcode_finish(cache_entry) == -1)) {
                errno = EIO;
                success = false;
                break;
            }

            if (cache_entry->suspend_timeout())
            {
                mp3fs_debug("Suspend timeout. Transcoding suspended for file '%s'.", cache_entry->filename().c_str());

                while (cache_entry->suspend_timeout() && !(timeout = cache_entry->decode_timeout()) && !thread_exit)
                {
                    sleep(1);
                }

                if (timeout)
                {
                    break;
                }

                mp3fs_debug("Transcoding resumed for file '%s'.", cache_entry->filename().c_str());
            }
        }
    }
    catch (bool _success)
    {
        cache_entry->unlock();

        success = _success;
    }

    cache_entry->m_transcoder->close();

    if (timeout || thread_exit)
    {
        cache_entry->m_is_decoding = false;
        cache_entry->m_cache_info.m_finished = false;
        cache_entry->m_cache_info.m_error = true;

        if (timeout)
        {
            mp3fs_debug("Timeout! Transcoding aborted for file '%s'.", cache_entry->filename().c_str());
            cache->close(&cache_entry, true);  // After timeout need to delete this here
        }
        else
        {
            mp3fs_debug("Thread exit! Transcoding aborted for file '%s'.", cache_entry->filename().c_str());
            cache_entry->close();
        }
    }
    else
    {
        cache_entry->m_cache_info.m_error = !success;
        cache_entry->close();

        mp3fs_debug("Transcoding complete for file '%s'. Success = %i", cache_entry->filename().c_str(), success);
    }

    thread_count--;

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
        mp3fs_debug("Invalid logging level string: %s\n", max_level);
        return false;
    }

    return InitLogging(logfile, it->second, to_stderr, to_syslog);
}
