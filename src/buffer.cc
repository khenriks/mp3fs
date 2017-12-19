/*
 * data buffer class source for ffmpegfs
 *
 * Copyright (C) 2013 K. Henriksson
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

#include "buffer.h"
#include "transcode.h"
#include "ffmpeg_utils.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sys/mman.h>
#include <libgen.h>

using namespace std;

/* Initially Buffer is empty. It will be allocated as needed. */
Buffer::Buffer(const string &filename)
    : m_mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
    , m_filename(filename)
    , m_buffer_pos(0)
    , m_buffer_watermark(0)
    , m_is_open(false)
    , m_buffer(NULL)
    , m_fd(-1)
{
    char cachepath[PATH_MAX];

    cache_path(cachepath, sizeof(cachepath));

    m_cachefile = cachepath;
    m_cachefile += params.m_mountpath;
    m_cachefile += filename;
    m_cachefile += ".cache.";
    m_cachefile += params.m_desttype;
}

/* If buffer_data was never allocated, this is a no-op. */
Buffer::~Buffer()
{
    close();
}

bool Buffer::open(bool erase_cache)
{
    if (m_is_open)
    {
        return true;
    }

    m_is_open = true;

    bool success = true;

    lock();
    try
    {
        struct stat sb;
        size_t filesize;
        void *p;

        // Create the path to the cache file
        char *cachefile = strdup(m_cachefile.c_str());
        if (mktree(dirname(cachefile), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) && errno != EEXIST)
        {
            ffmpegfs_error("Error creating cache directory '%s': %s", m_cachefile.c_str(), strerror(errno));
            free(cachefile);
            throw false;
        }
        errno = 0;  // reset EEXIST, error can safely be ignored here

        free(cachefile);

        m_buffer_size = 0;
        m_buffer = NULL;
        m_buffer_pos = 0;
        m_buffer_watermark = 0;

        if (erase_cache)
        {
            remove_file();
            errno = 0;  // ignore this error
        }

        m_fd = ::open(m_cachefile.c_str(), O_CREAT | O_RDWR, (mode_t)0644);
        if (m_fd == -1)
        {
            ffmpegfs_error("Error opening cache file '%s': %s", m_cachefile.c_str(), strerror(errno));
            throw false;
        }

        if (fstat(m_fd, &sb) == -1)
        {
            ffmpegfs_error("fstat failed for '%s': %s", m_cachefile.c_str(), strerror(errno));
            throw false;
        }

        if (!S_ISREG(sb.st_mode))
        {
            ffmpegfs_error("'%s' is not a file.", m_cachefile.c_str());
            throw false;
        }

        filesize = sb.st_size;

        if (!filesize)
        {
            // If empty set file size to 1 page
            filesize = sysconf (_SC_PAGESIZE);

            if (ftruncate(m_fd, filesize) == -1)
            {
                ffmpegfs_error("Error calling ftruncate() to 'stretch' the file '%s': %s", m_cachefile.c_str(), strerror(errno));
                throw false;
            }
        }
        else
        {
            m_buffer_pos = m_buffer_watermark = filesize;
        }

        p = mmap(0, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (p == MAP_FAILED)
        {
            ffmpegfs_error("File mapping failed for '%s': %s", m_cachefile.c_str(), strerror(errno));
            throw false;
        }

        m_buffer_size = filesize;
        m_buffer = (uint8_t*)p;
    }
    catch (bool _success)
    {
        success = _success;

        if (!success)
        {
            m_is_open = false;
            if (m_fd != -1)
            {
                ::close(m_fd);
                m_fd = -1;
            }
        }
    }
    unlock();

    return success;
}

bool Buffer::close(int flags /*= CLOSE_CACHE_NOOPT*/)
{
    bool success = true;

    if (!m_is_open)
    {
        if (CACHE_CHECK_BIT(CLOSE_CACHE_DELETE, flags))
        {
            remove_file();
            errno = 0;  // ignore this error
        }

        return true;
    }

    m_is_open = false;

    lock();

    // Write it now to disk
    flush();

    void *p = m_buffer;
    size_t size = m_buffer_size;
    int fd = m_fd;

    m_buffer = NULL;
    m_buffer_size = 0;
    m_buffer_pos = 0;
    m_fd = -1;

    if (munmap(p, size) == -1)
    {
        ffmpegfs_error("File unmapping failed: %s", strerror(errno));
        success = false;
    }

    if (ftruncate(fd, m_buffer_watermark) == -1)
    {
        ffmpegfs_error("Error calling ftruncate() to resize and close the file '%s': %s", m_cachefile.c_str(), strerror(errno));
        success = false;
    }

    ::close(fd);

    if (CACHE_CHECK_BIT(CLOSE_CACHE_DELETE, flags))
    {
        remove_file();
        errno = 0;  // ignore this error
    }

    unlock();

    return success;
}

bool Buffer::remove_file()
{
    if (unlink(m_cachefile.c_str()) && errno != ENOENT)
    {
        ffmpegfs_warning("Cannot unlink the file '%s': %s", m_cachefile.c_str(), strerror(errno));
        return false;
    }
    else
    {
        return true;
    }
}

bool Buffer::flush()
{
    if (!m_is_open)
    {
        return false;
    }

    lock();
    if (msync(m_buffer, m_buffer_size, MS_SYNC) == -1)
    {
        ffmpegfs_error("Could not sync '%s' to disk: %s", m_cachefile.c_str(), strerror(errno));
    }
    unlock();

    return true;
}

bool Buffer::clear()
{
    if (!m_is_open)
    {
        return false;
    }

    bool bSuccess = true;

    lock();

    m_buffer_pos = 0;
    m_buffer_watermark = 0;

    // If empty set file size to 1 page
    long filesize = sysconf (_SC_PAGESIZE);

    if (ftruncate(m_fd, filesize) == -1)
    {
        ffmpegfs_error("Error calling ftruncate() to clear the file '%s': %s", m_cachefile.c_str(), strerror(errno));
        bSuccess = false;
    }
    unlock();

    return bSuccess;
}

/*
 * Reserve memory without changing size to reduce re-allocations
 */
bool Buffer::reserve(size_t size)
{
    bool success = true;

    lock();

    m_buffer = (uint8_t*)mremap (m_buffer, m_buffer_size, size,  MREMAP_MAYMOVE);
    if (m_buffer != NULL)
    {
        m_buffer_size = size;
    }

    if (ftruncate(m_fd, m_buffer_size) == -1)
    {
        ffmpegfs_error("Error calling ftruncate() to resize the file '%s': %s", m_cachefile.c_str(), strerror(errno));
        success = false;
    }

    unlock();

    return ((m_buffer != NULL) && success);
}

/*
 * Write data to the current position in the Buffer. The position pointer
 * will be updated.
 */
size_t Buffer::write(const uint8_t* data, size_t length)
{

    lock();

    uint8_t* write_ptr = write_prepare(length);
    if (!write_ptr)
    {
        length = 0;
    }
    else
    {
        memcpy(write_ptr, data, length);
        increment_pos(length);
    }

    unlock();

    return length;
}

/*
 * Ensure the Buffer has sufficient space for a quantity of data and
 * return a pointer where the data may be written. The position pointer
 * should be updated afterward with increment_pos().
 */
uint8_t* Buffer::write_prepare(size_t length)
{
    if (reallocate(m_buffer_pos + length))
    {
        if (m_buffer_watermark < m_buffer_pos + length)
        {
            m_buffer_watermark = m_buffer_pos + length;
        }
        return m_buffer + m_buffer_pos;
    }
    else
    {
        return NULL;
    }
}

/*
 * Increment the location of the internal pointer. This cannot fail and so
 * returns void. It does not ensure the position is valid memory because
 * that is done by the write_prepare methods via reallocate.
 */
void Buffer::increment_pos(ptrdiff_t increment)
{
    m_buffer_pos += increment;
}

bool Buffer::seek(size_t pos)
{
    if (pos <= size())
    {
        m_buffer_pos = pos;
        return true;
    }
    else
    {
        m_buffer_pos = size();
        return false;
    }
}

/* Give the value of the internal read position pointer. */
size_t Buffer::tell() const
{
    return m_buffer_pos;
}

/* Give the value of the internal buffer size pointer. */
size_t Buffer::size() const
{
    return m_buffer_size;
}

/* Number of bytes written to buffer so far (may be less than m_buffer.size()) */
size_t Buffer::buffer_watermark() const
{
    return m_buffer_watermark;
}

/* Copy buffered data into output buffer. */
bool Buffer::copy(uint8_t* out_data, size_t offset, size_t bufsize)
{
    if (size() >= offset && m_buffer != NULL)
    {
        lock();

        if (size() < offset + bufsize)
        {
            bufsize = size() - offset - 1;
        }

        memcpy(out_data, m_buffer + offset, bufsize);
        unlock();

        return true;
    }
    else
    {
        errno = ENOMEM;
        return false;
    }
}

/*
 * Ensure the allocation has at least size bytes available. If not,
 * reallocate memory to make more available. Fill the newly allocated memory
 * with zeroes.
 */
bool Buffer::reallocate(size_t newsize)
{
    if (newsize > size())
    {
        size_t oldsize = size();

        if (!reserve(newsize))
        {
            return false;
        }

        ffmpegfs_trace("Buffer reallocate: %zu -> %zu.", oldsize, newsize);
    }
    return true;
}

void Buffer::lock()
{
    pthread_mutex_lock(&m_mutex);
}

void Buffer::unlock()
{
    pthread_mutex_unlock(&m_mutex);
}

const string & Buffer::filename() const
{
    return m_filename;
}

const string & Buffer::cachefile() const
{
    return m_cachefile;
}
