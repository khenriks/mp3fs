/*
 * Cache entry object class for ffmpegfs
 *
 * Copyright (c) 2017 by Norbert Schlia (nschlia@oblivion-software.de)
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
#include <assert.h>

using namespace std;

Cache_Entry::Cache_Entry(Cache *owner, const string & filename)
    : m_owner(owner)
    , m_mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
    , m_ref_count(0)
    , m_thread_id(0)
{
    m_cache_info.m_filename = filename;

    m_cache_info.m_target_format[0] = '\0';
    strncat(m_cache_info.m_target_format, params.m_desttype, sizeof(m_cache_info.m_target_format) - 1);

    m_buffer = new Buffer(m_cache_info.m_filename);

    clear();

    ffmpegfs_trace("Created new cache entry.");
}

Cache_Entry::~Cache_Entry()
{
    if (m_thread_id && m_thread_id != pthread_self())
    {
        // If not same thread, wait for other to finish
        ffmpegfs_warning("Waiting for thread id %zx to terminate.", m_thread_id);

        int s = pthread_join(m_thread_id, NULL);
        if (s != 0)
        {
            ffmpegfs_error("Error joining thread id %zu: %s", m_thread_id, strerror(s));
        }
        else
        {
            ffmpegfs_info("Thread id %zx has terminated.", m_thread_id);
        }
    }

    delete m_buffer;
    ffmpegfs_trace("Deleted buffer.");
}

void Cache_Entry::clear(int fetch_file_time)
{
    struct stat sb;

    m_is_decoding = false;

    // Initialise ID3v1.1 tag structure
    init_id3v1(&m_id3v1);

    m_cache_info.m_predicted_filesize = 0;
    m_cache_info.m_encoded_filesize = 0;
    m_cache_info.m_finished = false;
    m_cache_info.m_access_time = m_cache_info.m_creation_time = time(NULL);
    m_cache_info.m_error = false;

    if (fetch_file_time)
    {
        if (stat(filename().c_str(), &sb) == -1)
        {
            m_cache_info.m_file_time = 0;
            m_cache_info.m_file_size = 0;
        }
        else
        {
            m_cache_info.m_file_time = sb.st_mtime;
            m_cache_info.m_file_size = sb.st_size;
        }
    }

    if (m_buffer != NULL)
    {
        m_buffer->clear();
    }
}

bool Cache_Entry::read_info()
{
    return m_owner->read_info(m_cache_info);
}

bool Cache_Entry::write_info()
{
    return m_owner->write_info(m_cache_info);
}

bool Cache_Entry::delete_info()
{
    return m_owner->delete_info(m_cache_info.m_filename);
}

bool Cache_Entry::update_access(bool bUpdateDB /*= false*/)
{
    m_cache_info.m_access_time = time(NULL);

    if (bUpdateDB)
    {
        return m_owner->write_info(m_cache_info);
    }
    else
    {
        return true;
    }
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

    bool erase_cache = !read_info();    // If read_info fails, rebuild cache entry

    if (!create_cache)
    {
        return true;
    }

    // Store access time
    update_access(true);

    // Open the cache
    if (m_buffer->open(erase_cache))
    {
        return true;
    }
    else
    {
        clear(false);
        return false;
    }
}

void Cache_Entry::close_buffer(int flags)
{
    if (m_buffer->close(flags))
    {
        if (flags)
        {
            delete_info();
        }
    }
}

// Returns true if entry may be deleted, false if still in use
bool Cache_Entry::close(int flags)
{
    write_info();

    if (m_buffer == NULL)
    {
        errno = EINVAL;
        return false;
    }

    //lock();

    if (!m_ref_count)
    {
        close_buffer(flags);

        //unlock();

        return true;
    }

    if (__sync_sub_and_fetch(&m_ref_count, 1) > 0)
    {
        // Just flush to disk
        flush();
        return false;
    }

    close_buffer(flags);

    //unlock();

    return true;
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

size_t Cache_Entry::size() const
{
    if (m_cache_info.m_encoded_filesize)
    {
        return m_cache_info.m_encoded_filesize;
    }
    else
    {
        return m_cache_info.m_predicted_filesize;
    }
}

time_t Cache_Entry::age() const
{
    return (time(NULL) - m_cache_info.m_creation_time);
}

time_t Cache_Entry::last_access() const
{
    return m_cache_info.m_access_time;
}

bool Cache_Entry::expired() const
{
    return (age() > params.m_expiry_time);
}

bool Cache_Entry::suspend_timeout() const
{
    return ((time(NULL) - m_cache_info.m_access_time) > params.m_max_inactive_suspend);
}

bool Cache_Entry::decode_timeout() const
{
    return ((time(NULL) - m_cache_info.m_access_time) > params.m_max_inactive_abort);
}

const string & Cache_Entry::filename()
{
    return m_cache_info.m_filename;
}

void Cache_Entry::lock()
{
    pthread_mutex_lock(&m_mutex);
}

void Cache_Entry::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}

int Cache_Entry::ref_count() const
{
    return m_ref_count;
}

