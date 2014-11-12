/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
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

#include "mp3_encoder.h"
#include "transcode.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <map>
#include <pthread.h>
#include <string>
#include <sys/time.h>
#include <vector>

#include "coders.h"

using std::make_pair;
using std::map;
using std::numeric_limits;
using std::pair;
using std::sort;
using std::string;
using std::vector;

/*
 * Holds the size and modified time for a file, and is used in the file stats
 * cache.
 */
class FileStat {
public:
    FileStat(size_t _size, time_t _mtime);

    void update_atime();
    size_t get_size() const  { return size; }
    time_t get_atime() const { return atime; }
    time_t get_mtime() const { return mtime; }
private:
    size_t size;
    // The last time this object was accessed. Used to implement the most
    // recently used cache policy.
    time_t atime;
    // The modified time of the decoded file when the size was computed.
    time_t mtime;
};
    
/* Transcoder parameters for open mp3 */
struct transcoder {
    Buffer buffer;
    string filename;
    size_t encoded_filesize;

    Encoder* encoder;
    Decoder* decoder;
};

static map<string, FileStat> cached_stats;
static size_t cached_stats_size = 0;
static pthread_mutex_t cached_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

FileStat::FileStat(size_t _size, time_t _mtime) : size(_size), mtime(_mtime) {
    update_atime();
}

void FileStat::update_atime() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    atime = tv.tv_sec;
}

/*
 * Transcode the buffer until the buffer has enough or until an error occurs.
 * The buffer needs at least 'end' bytes before transcoding stops. Returns
 * true if no errors and false otherwise.
 */
static bool transcode_until(struct transcoder* trans, size_t end) {
    while (trans->encoder && trans->buffer.tell() < end) {
        int stat = trans->decoder->process_single_fr(trans->encoder,
                                                     &trans->buffer);
        if (stat == -1 || (stat == 1 && transcoder_finish(trans) == -1)) {
            errno = EIO;
            return false;
        }
    }
    return true;
}

/* Compute the size of a cache entry */

static size_t cached_entry_size(const pair<string, FileStat>& cache_entry) {
    return sizeof(pair<string, FileStat>) + cache_entry.first.capacity() +
            (3 * sizeof(size_t)) + // For GNU C++ string extra parts
            (3 * sizeof(void*));   // For map entry
}

/* Compare two FileStat objects by their access time */

bool cmp_by_atime(const pair<string, FileStat>& a1,
        const pair<string, FileStat>& a2) {
    return a1.second.get_atime() < a2.second.get_atime();
}

/*
 * Prune invalid and old cache entries until the cache is at 90% of
 * capacity. Assumes the cache is locked.
 */
static void prune_cache() {
    mp3fs_debug("Pruning stats cache");
    size_t target_size = params.statcachesize * 921; // 90% of 1k
    vector< pair<string, FileStat> > sorted_entries;

    /* First remove all invalid cache entries. */
    map<string, FileStat>::iterator next_p;
    for (map<string, FileStat>::iterator p = cached_stats.begin();
            p != cached_stats.end(); p = next_p) {
        const string& decoded_file = p->first;
        const FileStat& file_stat = p->second;
        next_p = p;
        ++next_p;

        struct stat s;
        if (stat(decoded_file.c_str(), &s) < 0 ||
                s.st_mtime > file_stat.get_mtime()) {
            mp3fs_debug("Removed out of date file '%s' from stats cache",
                    p->first.c_str());
            errno = 0;
            cached_stats_size -= cached_entry_size(*p);
            cached_stats.erase(p);
        } else {
            sorted_entries.push_back(*p);
        }
    }
    if (cached_stats_size <= target_size) {
        return;
    }

    // Sort all cache entries by the atime, and remove the oldest entries until
    // the cache size meets the target.
    sort(sorted_entries.begin(), sorted_entries.end(), cmp_by_atime);
    for (vector< pair<string, FileStat> >::iterator p = sorted_entries.begin();
            p != sorted_entries.end() && cached_stats_size > target_size;
            ++p) {
        mp3fs_debug("Pruned oldest file '%s' from stats cache",
                p->first.c_str());
        cached_stats_size -= cached_entry_size(*p);
        cached_stats.erase(p->first);
    }
}

/*
 * Get the file size from the cache for the given filename, if it exists.
 * Use 'mtime' as the modified time of the file to check for an invalid cache
 * entry. Return true if the file size was found.
 */
static bool get_cached_filesize(const string& filename, time_t mtime,
        size_t& filesize) {
    bool in_cache = false;
    pthread_mutex_lock(&cached_stats_mutex);
    map<string, FileStat>::iterator p = cached_stats.find(filename);
    if (p != cached_stats.end()) {
        FileStat& file_stat = p->second;
        if (mtime > file_stat.get_mtime()) {
            // The decoded file has changed since this entry was created, so
            // remove the invalid entry.
            mp3fs_debug("Removed out of date file '%s' from stats cache",
                    p->first.c_str());
            cached_stats_size -= cached_entry_size(*p);
            cached_stats.erase(p);
        } else {
            mp3fs_debug("Found file '%s' in stats cache with size %u",
                    p->first.c_str(), file_stat.get_size());
            in_cache = true;
            filesize = file_stat.get_size();
            file_stat.update_atime();
        }
    }
    pthread_mutex_unlock(&cached_stats_mutex);
    return in_cache;
}

/* Add or update an entry in the stats cache */

static void put_cached_filesize(const string& filename, size_t filesize,
        time_t mtime) {
    FileStat file_stat(filesize, mtime);
    pthread_mutex_lock(&cached_stats_mutex);
    map<string, FileStat>::iterator p = cached_stats.find(filename);
    if (p == cached_stats.end()) {
        mp3fs_debug("Added file '%s' to stats cache with size %u",
                filename.c_str(), file_stat.get_size());
        map<string, FileStat>::iterator inserted_p =
                cached_stats.insert(make_pair(filename, file_stat)).first;
        cached_stats_size += cached_entry_size(*inserted_p);
    } else if (mtime >= p->second.get_mtime()) {
        mp3fs_debug("Updated file '%s' in stats cache with size %u",
                filename.c_str(), file_stat.get_size());
        p->second = file_stat;
    }
    if (cached_stats_size > params.statcachesize * 1024) {
        prune_cache();
    }
    pthread_mutex_unlock(&cached_stats_mutex);
}

/* Use "C" linkage to allow access from C code. */
extern "C" {

/* Allocate and initialize the transcoder */

struct transcoder* transcoder_new(char* filename) {
    mp3fs_debug("Creating transcoder object for %s", filename);

    /* Allocate transcoder structure */
    struct transcoder* trans = new struct transcoder;
    if (!trans) {
        goto trans_fail;
    }

    /* Create Encoder and Decoder objects. */
    trans->filename = filename;
    trans->encoded_filesize = 0;
    trans->decoder = Decoder::CreateDecoder(strrchr(filename, '.') + 1);
    if (!trans->decoder) {
        goto decoder_fail;
    }

    mp3fs_debug("Ready to initialize decoder.");

    if (trans->decoder->open_file(filename) == -1) {
        goto init_fail;
    }

    mp3fs_debug("Decoder initialized successfully.");

    get_cached_filesize(trans->filename, trans->decoder->mtime(),
            trans->encoded_filesize);
    trans->encoder = Encoder::CreateEncoder(params.desttype,
            trans->encoded_filesize);
    if (!trans->encoder) {
        goto encoder_fail;
    }

    /*
     * Process metadata. The Decoder will call the Encoder to set appropriate
     * tag values for the output file.
     */
    if (trans->decoder->process_metadata(trans->encoder) == -1) {
        mp3fs_debug("Error processing metadata.");
        goto init_fail;
    }

    mp3fs_debug("Metadata processing finished.");

    /* Render tag from Encoder to Buffer. */
    if (trans->encoder->render_tag(trans->buffer) == -1) {
        mp3fs_debug("Error rendering tag in Encoder.");
        goto init_fail;
    }

    mp3fs_debug("Tag written to Buffer.");

    return trans;

encoder_fail:
    delete trans->decoder;
decoder_fail:
init_fail:
    delete trans->encoder;

    delete trans;

trans_fail:
    return NULL;
}

/* Read some bytes into the internal buffer and into the given buffer. */

ssize_t transcoder_read(struct transcoder* trans, char* buff, off_t offset,
                        size_t len) {
    mp3fs_debug("Reading %zu bytes from offset %jd.", len, (intmax_t)offset);
    if ((size_t)offset > transcoder_get_size(trans)) {
        return -1;
    }
    if (offset + len > transcoder_get_size(trans)) {
        len = transcoder_get_size(trans) - offset;
    }

    // TODO: Avoid favoring MP3 in program structure.
    /*
     * If we are encoding to MP3 and the requested data overlaps the ID3v1 tag
     * at the end of the file, do not encode data first up to that position.
     * This optimizes the case where applications read the end of the file
     * first to read the ID3v1 tag.
     */
    if (strcmp(params.desttype, "mp3") == 0 &&
        (size_t)offset > trans->buffer.tell()
        && offset + len >
        (transcoder_get_size(trans) - Mp3Encoder::id3v1_tag_length)) {
        trans->buffer.copy_into((uint8_t*)buff, offset, len);

        return len;
    }

    bool success = true;
    if (trans->decoder && trans->encoder) {
        if (strcmp(params.desttype, "mp3") == 0 && params.vbr) {
            /*
             * The Xing data (which is pretty close to the beginning of the
             * file) cannot be determined until the entire file is encoded, so
             * transcode the entire file for any read.
             */
            success = transcode_until(trans, numeric_limits<size_t>::max());
        } else {
            success = transcode_until(trans, offset + len);
        }
    }
    if (!success) {
        return -1;
    }

    // truncate if we didn't actually get len
    if (trans->buffer.tell() < (size_t) offset) {
        len = 0;
    } else if (trans->buffer.tell() < offset + len) {
        len = trans->buffer.tell() - offset;
    }

    trans->buffer.copy_into((uint8_t*)buff, offset, len);

    return len;
}

/* Close the input file and free everything but the initial buffer. */

int transcoder_finish(struct transcoder* trans) {
    // flac cleanup
    time_t decoded_file_mtime = 0;
    if (trans->decoder) {
        decoded_file_mtime = trans->decoder->mtime();
        delete trans->decoder;
        trans->decoder = NULL;
    }

    // lame cleanup
    if (trans->encoder) {
        int len = trans->encoder->encode_finish(trans->buffer);
        if (len == -1) {
            return -1;
        }

        /* Check encoded buffer size. */
        trans->encoded_filesize = trans->encoder->get_actual_size();
        mp3fs_debug("Finishing file. Predicted size: %zu, final size: %zu",
                    trans->encoder->calculate_size(), trans->encoded_filesize);
        delete trans->encoder;
        trans->encoder = NULL;
    }

    if (params.statcachesize > 0 && trans->encoded_filesize != 0) {
        put_cached_filesize(trans->filename, trans->encoded_filesize,
                decoded_file_mtime);
    }

    return 0;
}

/* Free the transcoder structure. */

void transcoder_delete(struct transcoder* trans) {
    if (trans->decoder) {
        delete trans->decoder;
    }
    if (trans->encoder) {
        delete trans->encoder;
    }
    delete trans;
}

/* Return size of output file, as computed by Encoder. */
size_t transcoder_get_size(struct transcoder* trans) {
    if (trans->encoded_filesize != 0) {
        return trans->encoded_filesize;
    } else if (trans->encoder) {
        return trans->encoder->calculate_size();
    } else {
        return trans->buffer.tell();
    }
}

}
