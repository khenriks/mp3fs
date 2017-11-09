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
    : m_thread_id(0)
    , m_ref_count(0)
    , m_transcoder(new FFMPEG_Transcoder)
{
    reset();

    char dir[PATH_MAX];

    tempdir(dir, sizeof(dir));

    m_filename = filename;

    m_cachefile = dir;
    m_cachefile += "/mp3fs/";
    m_cachefile += params.mountpath;
    m_cachefile += "/";
    char *p = strdup(filename);
    m_cachefile += basename(p);
    free(p);

    m_buffer = new Buffer(m_filename, m_cachefile);

    mp3fs_debug("Created new Cache_Entry.");
    fprintf(stderr, "Created new Cache_Entry.\n");
}

Cache_Entry::~Cache_Entry()
{
    if (m_thread_id)
    {
        mp3fs_debug("Waiting for thread id %zx to terminate.", m_thread_id);
        fprintf(stderr, "Waiting for thread id %zx to terminate.\n", m_thread_id);
        int s = pthread_join(m_thread_id, NULL);
        if (s != 0)
        {
            mp3fs_error("Error joining thread id %zx: %s", m_thread_id, strerror(s));
        }
        else
        {
            mp3fs_debug("Thread id %zx has terminated.", m_thread_id);
            fprintf(stderr, "Thread id %zx has terminated.\n", m_thread_id);
        }
    }

    delete m_buffer;
    delete m_transcoder;
    mp3fs_debug("Deleted Cache_Entry.");
    fprintf(stderr, "Deleted Cache_Entry.\n");
}

string Cache_Entry::info_file() const
{
    return (m_cachefile + ".info");
}

void Cache_Entry::reset()
{
    m_encoded_filesize = 0;
    m_is_decoding = false;
    m_finished = false;
    m_access_time = m_creation_time = time(NULL);
    m_error = false;
}

bool Cache_Entry::read_info()
{
    ifstream input_file;

    reset();

    input_file.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
    try {
        input_file.open(info_file(), ios::in | ios::binary);

        input_file.read((char*)&m_encoded_filesize, sizeof(m_encoded_filesize));
        input_file.read((char*)&m_finished, sizeof(m_finished));
        input_file.read((char*)&m_error, sizeof(m_error));
        input_file.read((char*)&m_creation_time, sizeof(m_creation_time));
        input_file.read((char*)&m_access_time, sizeof(m_access_time));

        input_file.close();

        std::cerr << "READ" << std::endl;
    }
    catch (std::ifstream::failure e) {
        reset();
        std::cerr << "RESET" << std::endl;
                std::cerr << "Caught an ios_base::failure rading file." << endl
                          << "File: " << info_file()  << endl
                          << "Explanatory string: " << e.what() << endl
                          << "Error code: " << e.code() << endl;
        return false;
    }

    std::cerr << "m_encoded_filesize: " << m_encoded_filesize << std::endl;
    std::cerr << "m_finished: " << m_finished << std::endl;
    std::cerr << "m_error: " << m_error << std::endl;

    return true;
}

bool Cache_Entry::write_info()
{
    ofstream output_file;
    output_file.exceptions ( std::ifstream::failbit | std::ifstream::badbit );
    try {
        output_file.open(info_file(), ios::out | ios::binary);

        output_file.write((char*)&m_encoded_filesize, sizeof(m_encoded_filesize));
        output_file.write((char*)&m_finished, sizeof(m_finished));
        output_file.write((char*)&m_error, sizeof(m_error));
        output_file.write((char*)&m_creation_time, sizeof(m_creation_time));
        output_file.write((char*)&m_access_time, sizeof(m_access_time));

        output_file.close();
    }
    catch (std::ifstream::failure e) {
        //        std::cerr << "Caught an ios_base::failure writing file." << endl
        //                  << "File: " << info_file()  << endl
        //                  << "Explanatory string: " << e.what() << endl
        //                  << "Error code: " << e.code() << endl;
        return false;
    }

    return true;
}

bool Cache_Entry::open(bool create_cache /*= true*/)
{
    if (m_buffer == NULL)
    {
        errno = EINVAL;
        return false;
    }

    fprintf(stderr, "************************************** CACHE OPEN ------> ref_count = %i\n", m_ref_count);

    if (__sync_fetch_and_add(&m_ref_count, 1) > 0)
    {
        fprintf(stderr, "************************************** CACHE CLOSE DELAYED ref_count = %i\n", m_ref_count);
        fflush(stderr);
        return true;
    }

    fprintf(stderr, "************************************** CACHE OPEN ======> ref_count = %i\n", m_ref_count);
    fflush(stderr);

    if (!create_cache)
    {
        fprintf(stderr, "************************************** NOT creating cache\n");
        fflush(stderr);
        return true;
    }

    fprintf(stderr, "************************************** CREATING CACHE\n");
    fflush(stderr);

    // Open the cache
    if (m_buffer->open())
    {
        read_info();
        return true;
    }
    else
    {
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

    fprintf(stderr, "************************************** CACHE CLOSE ------> ref_count = %i\n", m_ref_count);
    fflush(stderr);

    if (!m_ref_count)
    {
        fprintf(stderr, "************************************** CACHE CLOSE IGNORED\n");
        fflush(stderr);
        return true;
    }

    if (__sync_sub_and_fetch(&m_ref_count, 1) > 0)
    {
        // Just flush to disk
        flush();
        fprintf(stderr, "************************************** CACHE CLOSE DELAYED ref_count = %i\n", m_ref_count);
        fflush(stderr);
        return true;
    }

    fprintf(stderr, "************************************** CACHE CLOSE ======> ref_count = %i\n", m_ref_count);
    fflush(stderr);

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
    return (time(NULL) - m_creation_time);
}

time_t Cache_Entry::last_access() const
{
    return m_access_time;
}

bool Cache_Entry::expired() const
{
    return ((time(NULL) - m_creation_time) > params.expiry_time);
}

bool Cache_Entry::suspend_timeout() const
{
    return ((time(NULL) - m_access_time) > params.max_inactive_suspend);
}

bool Cache_Entry::decode_timeout() const
{
    return ((time(NULL) - m_access_time) > params.max_inactive_abort);
}
