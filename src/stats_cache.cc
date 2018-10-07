/*
 * File stats cache source for mp3fs
 *
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

#include "stats_cache.h"

#include <algorithm>
#include <cerrno>
#include <sys/stat.h>
#include <vector>

#include "logging.h"
#include "transcode.h"

namespace {

/* Compare two cache entries by the access time of the FileStat objects. */

bool cmp_by_atime(const StatsCache::cache_entry_t& a1,
        const StatsCache::cache_entry_t& a2) {
    return a1.second.get_atime() < a2.second.get_atime();
}

}

FileStat::FileStat(size_t _size, time_t _mtime) : size(_size), mtime(_mtime) {
    update_atime();
}

void FileStat::update_atime() {
    atime = time(nullptr);
}

bool FileStat::operator==(const FileStat& other) const {
    return size == other.size && atime == other.atime && mtime == other.mtime;
}

/*
 * Get the file size from the cache for the given filename, if it exists.
 * Use 'mtime' as the modified time of the file to check for an invalid cache
 * entry. Return true if the file size was found.
 */
bool StatsCache::get_filesize(const std::string& filename, time_t mtime,
        size_t& filesize) {
    bool in_cache = false;
    pthread_mutex_lock(&mutex);
    cache_t::iterator p = cache.find(filename);
    if (p != cache.end()) {
        FileStat& file_stat = p->second;
        if (mtime > file_stat.get_mtime()) {
            // The decoded file has changed since this entry was created, so
            // remove the invalid entry.
            Log(DEBUG) << "Removed out of date file '" <<  p->first <<
                    "' from stats cache";
            cache.erase(p);
        } else {
            Log(DEBUG) << "Found file '" << p->first <<
                    "' in stats cache with size " << file_stat.get_size();
            in_cache = true;
            filesize = file_stat.get_size();
            file_stat.update_atime();
        }
    }
    pthread_mutex_unlock(&mutex);
    return in_cache;
}

/* Add or update an entry in the stats cache */

void StatsCache::put_filesize(const std::string& filename, size_t filesize,
        time_t mtime) {
    FileStat file_stat(filesize, mtime);
    pthread_mutex_lock(&mutex);
    cache_t::iterator p = cache.find(filename);
    if (p == cache.end()) {
        Log(DEBUG) << "Added file '" << filename <<
                "' to stats cache with size " << file_stat.get_size();
        cache.insert(std::make_pair(filename, file_stat));
    } else if (mtime >= p->second.get_mtime()) {
        Log(DEBUG) << "Updated file '" << filename <<
                "' in stats cache with size " << file_stat.get_size();
        p->second = file_stat;
    }
    bool needs_pruning = cache.size() > params.statcachesize;
    pthread_mutex_unlock(&mutex);
    if (needs_pruning) {
        prune();
    }
}

/*
 * Prune invalid and old cache entries until the cache is at 90% of capacity.
 */
void StatsCache::prune() {
    Log(DEBUG) << "Pruning stats cache";
    size_t target_size = 9 * params.statcachesize / 10; // 90%
    typedef std::vector<cache_entry_t> entry_vector;
    entry_vector sorted_entries;

    /* Copy all the entries to a vector to be sorted. */
    pthread_mutex_lock(&mutex);
    sorted_entries.reserve(cache.size());
    for (cache_t::iterator p = cache.begin(); p != cache.end(); ++p) {
        /* Force a true copy of the string to prevent multithreading issues. */
        std::string file(p->first.c_str());
        sorted_entries.push_back(std::make_pair(file, p->second));
    }
    pthread_mutex_unlock(&mutex);
    /* Sort the entries by access time, with the oldest first */
    sort(sorted_entries.begin(), sorted_entries.end(), cmp_by_atime);

    /*
     * Remove all invalid cache entries. Don't bother removing invalid
     * entries from sorted_entries, as the removal from a vector can have bad
     * performance and removing the entry twice (once here and once in the next
     * loop) is harmless.
     *
     * Lock the cache for each entry removed instead of putting the lock around
     * the entire loop because the stat() can be expensive.
     */
    for (entry_vector::iterator p = sorted_entries.begin();
            p != sorted_entries.end(); ++p) {
        const std::string& decoded_file = p->first;
        const FileStat& file_stat = p->second;
        struct stat s;
        if (stat(decoded_file.c_str(), &s) < 0 ||
                s.st_mtime > file_stat.get_mtime()) {
            Log(DEBUG) << "Removed out of date file '" << decoded_file <<
                    "' from stats cache";
            errno = 0;
            pthread_mutex_lock(&mutex);
            remove_entry(decoded_file, file_stat);
            pthread_mutex_unlock(&mutex);
        }
    }

    /* Remove the oldest entries until the cache size meets the target. */
    pthread_mutex_lock(&mutex);
    for (entry_vector::iterator p = sorted_entries.begin();
            p != sorted_entries.end() && cache.size() > target_size; ++p) {
        Log(DEBUG) << "Pruned oldest file '" << p->first <<
                "' from stats cache";
        remove_entry(p->first, p->second);
    }
    pthread_mutex_unlock(&mutex);
}

/*
 * Remove the cache entry if it exists and if the entry's file stat matches the
 * given file stat, i.e. the file stat hasn't changed.  Assumes the cache is
 * locked.
 */
void StatsCache::remove_entry(const std::string& file,
        const FileStat& file_stat) {
    cache_t::iterator p = cache.find(file);
    if (p != cache.end() && p->second == file_stat) {
        cache.erase(p);
    }
}
