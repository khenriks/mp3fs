/*
 * data buffer class source for mp3fs
 *
 * Copyright (C) 2013 K. Henriksson
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

/* Initially Buffer is empty. It will be allocated as needed. */
Buffer::Buffer(const std::string &filename, const std::string &cachefile)
    : mutex(PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP)
    , m_filename(filename)
    , m_cachefile(cachefile)
    , m_buffer_pos(0)
    , m_buffer_watermark(0)
    #ifdef _USE_DISK
    , m_buffer(NULL)
    , m_fd(-1)
    #endif
{
}

/* If buffer_data was never allocated, this is a no-op. */
Buffer::~Buffer() {
    close();
}

std::string Buffer::cache_file() const
{
    return (m_cachefile + ".cache");
}

bool Buffer::open()
{
#ifdef _USE_DISK 
    if (m_buffer != NULL)
    {
        return true;
    }

    struct stat sb;
    size_t filesize;
    void *p;
    bool success = true;

    pthread_mutex_lock(&mutex);
    try
    {
        // Create the path to the cache file
        char *cachefile = strdup(m_cachefile.c_str());
        if (mktree(dirname(cachefile), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) && errno != EEXIST)
        {
            mp3fs_error("Error creating cache directory '%s': %s", m_cachefile.c_str(), strerror(errno));
            free(cachefile);
            throw false;
        }
        free(cachefile);

        m_buffer_size = 0;
        m_buffer = NULL;

        m_fd = ::open(cache_file().c_str(), O_CREAT | O_RDWR, (mode_t)0644);
        if (m_fd == -1)
        {
            mp3fs_error("Error opening cache file '%s': %s", cache_file().c_str(), strerror(errno));
            throw false;
        }

        if (fstat(m_fd, &sb) == -1) {
            mp3fs_error("fstat failed for '%s': %s", cache_file().c_str(), strerror(errno));
            throw false;
        }

        if (!S_ISREG(sb.st_mode)) {
            mp3fs_error("'%s' is not a file.", cache_file().c_str());
            throw false;
        }

        filesize = sb.st_size;

        if (!filesize)
        {
            // If empty set file size to 1 page
            filesize = sysconf (_SC_PAGESIZE);

            if (ftruncate(m_fd, filesize) == -1)
            {
                mp3fs_error("Error calling ftruncate() to 'stretch' the file '%s': %s", cache_file().c_str(), strerror(errno));
                throw false;
            }

            //        if (lseek(m_fd, filesize - 1, SEEK_SET) == -1)
            //        {
            //            mp3fs_error("Error calling lseek() to 'stretch' the file '%s': %s", cache_file().c_str(), strerror(errno));
            //            throw false;;
            //        }

            //        /* Something needs to be written at the end of the file to
            //     * have the file actually have the new size.
            //     * Just writing an empty string at the current file position will do.
            //     *
            //     * Note:
            //     *  - The current position in the file is at the end of the stretched
            //     *    file due to the call to lseek().
            //     *  - An empty string is actually a single '\0' character, so a zero-byte
            //     *    will be written at the last byte of the file.
            //     */

            //        if (::write(m_fd, "", 1) == -1)
            //        {
            //            mp3fs_error("Error writing last byte of the file '%s': %s", cache_file().c_str(), strerror(errno));
            //            throw false;
            //        }
        }
        else
        {
            m_buffer_pos = m_buffer_watermark = filesize;
        }

        p = mmap(0, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (p == MAP_FAILED) {
            mp3fs_error("File mapping failed for '%s': %s", cache_file().c_str(), strerror(errno));
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
            if (m_fd != -1)
            {
                ::close(m_fd);
                m_fd = -1;
            }
        }
    }
    pthread_mutex_unlock(&mutex);

    return success;
#else
    return true;
#endif
}

bool Buffer::close(bool erase_cache)
{
#ifdef _USE_DISK
    if (m_buffer == NULL)
    {
        return true;
    }

    bool success = true;

    pthread_mutex_lock(&mutex);

    // Write it now to disk
    flush();

    void *p = m_buffer;

    m_buffer = NULL;

    if (munmap(p, m_buffer_size) == -1) {
        mp3fs_error("File unmapping failed: %s", strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        success = false;
    }

    if (ftruncate(m_fd, m_buffer_watermark) == -1)
    {
        mp3fs_error("Error calling ftruncate() to 'stretch' the file '%s': %s", cache_file().c_str(), strerror(errno));
        success = false;
    }

    ::close(m_fd);
    m_fd = -1;

    if (erase_cache)
    {
        if (unlink(cache_file().c_str()) && errno != ENOENT)
        {
            mp3fs_warning("Cannot unlink the file '%s': %s", cache_file().c_str(), strerror(errno));
        }
        errno = 0;  // ignore this error
    }

    pthread_mutex_unlock(&mutex);

#endif
    return success;
}

bool Buffer::flush()
{
    if (m_buffer == NULL)
    {
        return false;
    }

    pthread_mutex_lock(&mutex);
    if (msync(m_buffer, m_buffer_size, MS_SYNC) == -1)
    {
        mp3fs_error("Could not sync the file to disk: %s", strerror(errno));
    }
    pthread_mutex_unlock(&mutex);

    return true;
}

/*
 * Reserve memory without changing size to reduce re-allocations
 */
bool Buffer::reserve(size_t size) {

#ifdef _USE_DISK
    bool success = true;

    pthread_mutex_lock(&mutex);

    m_buffer = (uint8_t*)mremap (m_buffer, m_buffer_size, size,  MREMAP_MAYMOVE);
    m_buffer_size = size;

    if (ftruncate(m_fd, m_buffer_size) == -1)
    {
        mp3fs_error("Error calling ftruncate() to 'stretch' the file '%s': %s", cache_file().c_str(), strerror(errno));
        success = false;
    }

    pthread_mutex_unlock(&mutex);

    return ((m_buffer != NULL) && success);
#else
    try
    {
        m_buffer.reserve(size);
    }
    catch (std::exception& /*e*/)
    {
        return false;
    }

    return true;
#endif
}

/*
 * Write data to the current position in the Buffer. The position pointer
 * will be updated.
 */
size_t Buffer::write(const uint8_t* data, size_t length) {

    pthread_mutex_lock(&mutex);

    uint8_t* write_ptr = write_prepare(length);
    if (!write_ptr) {
        length = 0;
    }
    else
    {
        memcpy(write_ptr, data, length);
        increment_pos(length);
    }

    pthread_mutex_unlock(&mutex);

    return length;
}

/*
 * Ensure the Buffer has sufficient space for a quantity of data and
 * return a pointer where the data may be written. The position pointer
 * should be updated afterward with increment_pos().
 */
uint8_t* Buffer::write_prepare(size_t length) {
    if (reallocate(m_buffer_pos + length)) {
        if (m_buffer_watermark < m_buffer_pos + length) {
            m_buffer_watermark = m_buffer_pos + length;
        }
#ifdef _USE_DISK
        return m_buffer + m_buffer_pos;
#else
        return m_buffer.data() + m_buffer_pos;
#endif
    } else {
        return NULL;
    }
}

/*
 * Increment the location of the internal pointer. This cannot fail and so
 * returns void. It does not ensure the position is valid memory because
 * that is done by the write_prepare methods via reallocate.
 */
void Buffer::increment_pos(ptrdiff_t increment) {
    m_buffer_pos += increment;
}

bool Buffer::seek(size_t pos) {
    if (pos <= size()) {
        m_buffer_pos = pos;
        return true;
    }
    else {
        m_buffer_pos = size();
        return false;
    }
}

/* Give the value of the internal read position pointer. */
size_t Buffer::tell() const {
    return m_buffer_pos;
}

/* Give the value of the internal buffer size pointer. */
size_t Buffer::size() const {
#ifdef _USE_DISK
    return m_buffer_size;
#else
    return m_buffer.size();
#endif
}

/* Number of bytes written to buffer so far (may be less than m_buffer.size()) */
size_t Buffer::buffer_watermark() const {
    return m_buffer_watermark;
}

/* Copy buffered data into output buffer. */
void Buffer::copy(uint8_t* out_data, size_t offset, size_t bufsize) {
    if (size() >= offset)
    {
        pthread_mutex_lock(&mutex);

        if (size() < offset + bufsize)
        {
            bufsize = size() - offset - 1;
        }
#ifdef _USE_DISK
        memcpy(out_data, m_buffer + offset, bufsize);
        pthread_mutex_unlock(&mutex);
#else
        memcpy(out_data, m_buffer.data() + offset, bufsize);
#endif
    }
}

/*
 * Ensure the allocation has at least size bytes available. If not,
 * reallocate memory to make more available. Fill the newly allocated memory
 * with zeroes.
 */
bool Buffer::reallocate(size_t newsize) {
    if (newsize > size()) {
        size_t oldsize = size();
#ifdef _USE_DISK
        if (!reserve(newsize))
        {
            return false;
        }
#else
        try
        {
            m_buffer.resize(newsize, 0);
        }
        catch (std::exception& /*e*/)
        {
            return false;
        }
#endif

        mp3fs_debug("Buffer reallocate: %lu -> %lu.", oldsize, newsize);
    }
    return true;
}
