/*
 * Cache entry object class for mp3fs
 *
 * Copyright (c) 2017 by Norbert Schlia (nschlia@oblivon-software.de)
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

#include "cache_entry.h"
#include "ffmpeg_transcoder.h"
#include "buffer.h"
#include "transcode.h"

#include <unistd.h>
#include <fstream>
#include <iostream>
#include <libgen.h>

using namespace std;

Cache_Entry::Cache_Entry(const char *filename)
    : m_mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
    , m_thread_id(0)
    , m_ref_count(0)
    , m_transcoder(new FFMPEG_Transcoder)
{
    char dir[PATH_MAX];

    tempdir(dir, sizeof(dir));

    m_filename = filename;

    m_cachefile = dir;
    m_cachefile += "/mp3fs";
    m_cachefile += params.mountpath;
    //    m_cachefile += "/";
    //    char *p = strdup(filename);
    //    m_cachefile += basename(p);
    //    free(p);
    m_cachefile += filename;

    m_buffer = new Buffer(m_filename, m_cachefile);

    reset();

    mp3fs_debug("Created new Cache_Entry.");
}

Cache_Entry::~Cache_Entry()
{
    if (m_thread_id)
    {
        mp3fs_debug("Waiting for thread id %zx to terminate.", m_thread_id);

        int s = pthread_join(m_thread_id, NULL);
        if (s != 0)
        {
            mp3fs_error("Error joining thread id %zx: %s", m_thread_id, strerror(s));
        }
        else
        {
            mp3fs_debug("Thread id %zx has terminated.", m_thread_id);
        }
    }

    delete m_buffer;
    delete m_transcoder;
    mp3fs_debug("Deleted Cache_Entry.");
}

string Cache_Entry::info_file() const
{
    return (m_cachefile + ".info");
}

void Cache_Entry::reset(int fetch_file_time)
{
    struct stat sb;

    m_is_decoding = false;

    m_info.m_encoded_filesize = 0;
    m_info.m_finished = false;
    m_info.m_access_time = m_info.m_creation_time = time(NULL);
    m_info.m_error = false;

    if (fetch_file_time) {
        if (stat(m_filename.c_str(), &sb) == -1) {
            m_info.m_file_time = 0;
            m_info.m_file_size = 0;
        }
        else {
            m_info.m_file_time = sb.st_mtime;
            m_info.m_file_size = sb.st_size;
        }
    }
}

bool Cache_Entry::read_info()
{
    bool success = true;
    FILE *fpi = NULL;

    reset();

    // Removed C++ ifstream code, cause ABI troubles, see https://github.com/Alexpux/MINGW-packages/issues/747

    try
    {
        time_t file_time = m_info.m_file_time;
        uint64_t file_size = m_info.m_file_size;
        size_t n;

        reset();

        fpi = fopen(info_file().c_str(), "rb");
        if (fpi == NULL)
        {
            throw (int)errno;
        }

        n = fread((char*)&m_info.m_encoded_filesize, 1, sizeof(m_info.m_encoded_filesize), fpi);
        if (n != sizeof(m_info.m_encoded_filesize))
        {
            throw (int)ferror(fpi);
        }

        n = fread((char*)&m_info.m_finished, 1, sizeof(m_info.m_finished), fpi);
        if (n != sizeof(m_info.m_finished))
        {
            throw (int)ferror(fpi);
        }

        n = fread((char*)&m_info.m_error, 1, sizeof(m_info.m_error), fpi);
        if (n != sizeof(m_info.m_error))
        {
            throw (int)ferror(fpi);
        }

        n = fread((char*)&m_info.m_creation_time, 1, sizeof(m_info.m_creation_time), fpi);
        if (n != sizeof(m_info.m_creation_time))
        {
            throw (int)ferror(fpi);
        }

        n = fread((char*)&m_info.m_access_time, 1, sizeof(m_info.m_access_time), fpi);
        if (n != sizeof(m_info.m_access_time))
        {
            throw (int)ferror(fpi);
        }

        n = fread((char*)&m_info.m_file_time, 1, sizeof(m_info.m_file_time), fpi);
        if (n != sizeof(m_info.m_file_time))
        {
            throw (int)ferror(fpi);
        }

        if (file_time != m_info.m_file_time)
        {
            reset(false);

            m_info.m_file_time = file_time;

            mp3fs_info("File date changed '%s': rebuilding file.", info_file().c_str());

            success = false;
        }

        n = fread((char*)&m_info.m_file_size, 1, sizeof(m_info.m_file_size), fpi);
        if (n != sizeof(m_info.m_file_size))
        {
            throw (int)ferror(fpi);
        }

        if (file_size != m_info.m_file_size)
        {
            reset(false);

            m_info.m_file_size = file_size;

            mp3fs_info("File size changed '%s': rebuilding file.", info_file().c_str());

            success = false;
        }
    }
    catch (int error)
    {
        mp3fs_warning("Unable to read file '%s': %s", info_file().c_str(), strerror(error));
        reset();
        success = false;
    }

    if (fpi != NULL)
    {
        fclose(fpi);
    }

    return success;
}

bool Cache_Entry::write_info()
{
    bool success = true;
    FILE *fpo = NULL;

    try {
        size_t n;

        fpo = fopen(info_file().c_str(), "wb");
        if (fpo == NULL)
        {
            throw (int)errno;
        }

        n = fwrite((char*)&m_info.m_encoded_filesize, 1, sizeof(m_info.m_encoded_filesize), fpo);
        if (n != sizeof(m_info.m_encoded_filesize))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_finished, 1, sizeof(m_info.m_finished), fpo);
        if (n != sizeof(m_info.m_finished))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_error, 1, sizeof(m_info.m_error), fpo);
        if (n != sizeof(m_info.m_error))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_creation_time, 1, sizeof(m_info.m_creation_time), fpo);
        if (n != sizeof(m_info.m_creation_time))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_access_time, 1, sizeof(m_info.m_access_time), fpo);
        if (n != sizeof(m_info.m_access_time))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_file_time, 1, sizeof(m_info.m_file_time), fpo);
        if (n != sizeof(m_info.m_file_time))
        {
            throw (int)ferror(fpo);
        }

        n = fwrite((char*)&m_info.m_file_size, 1, sizeof(m_info.m_file_size), fpo);
        if (n != sizeof(m_info.m_file_size))
        {
            throw (int)ferror(fpo);
        }
    }
    catch (int error)
    {
        mp3fs_warning("Unable to update file '%s': %s", info_file().c_str(), strerror(error));
        reset();
        success = false;
    }

    if (fpo != NULL)
    {
        fclose(fpo);
    }

    return success;
}

bool Cache_Entry::open(bool create_cache /*= true*/)
{
    if (m_buffer == NULL)
    {
        errno = EINVAL;
        return false;
    }

    if (__sync_fetch_and_add(&m_ref_count, 1) > 0)
    {
        return true;
    }

    if (!create_cache)
    {
        return true;
    }

    bool erase_cache = !read_info();

    // Open the cache
    if (m_buffer->open(erase_cache))
    {
        return true;
    }
    else
    {
        reset(false);
        return false;
    }
}

bool Cache_Entry::close(bool erase_cache /*= false*/)
{
    if (m_buffer == NULL)
    {
        errno = EINVAL;
        return false;
    }

    if (!m_ref_count)
    {
        return true;
    }

    if (__sync_sub_and_fetch(&m_ref_count, 1) > 0)
    {
        // Just flush to disk
        flush();
        return true;
    }

    if (m_buffer->close(erase_cache))
    {
        if (erase_cache)
        {
            if (unlink(info_file().c_str()) && errno != ENOENT)
            {
                mp3fs_warning("Cannot unlink the file '%s': %s", info_file().c_str(), strerror(errno));
            }
            errno = 0;  // ignore this error
        }

        return true;
    }
    else
    {
        return false;
    }
}

bool Cache_Entry::flush()
{
    if (m_buffer == NULL)
    {
        errno = EINVAL;
        return false;
    }

    m_buffer->flush();
    write_info();

    return true;
}

time_t Cache_Entry::mtime() const
{
    return m_transcoder->mtime();
}

size_t Cache_Entry::calculate_size() const
{
    return m_transcoder->calculate_size();
}

const ID3v1 * Cache_Entry::id3v1tag() const
{
    return m_transcoder->id3v1tag();
}

time_t Cache_Entry::age() const
{
    return (time(NULL) - m_info.m_creation_time);
}

time_t Cache_Entry::last_access() const
{
    return m_info.m_access_time;
}

bool Cache_Entry::expired() const
{
    return ((time(NULL) - m_info.m_creation_time) > params.expiry_time);
}

bool Cache_Entry::suspend_timeout() const
{
    return ((time(NULL) - m_info.m_access_time) > params.max_inactive_suspend);
}

bool Cache_Entry::decode_timeout() const
{
    return ((time(NULL) - m_info.m_access_time) > params.max_inactive_abort);
}

void Cache_Entry::lock()
{
    pthread_mutex_lock(&m_mutex);
}

void Cache_Entry::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}
