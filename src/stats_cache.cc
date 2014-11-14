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
#include "transcode.h"

#include <algorithm>
#include <cerrno>
#include <vector>

FileStat::FileStat(size_t _size, time_t _mtime) : size(_size), mtime(_mtime) {
    update_atime();
}

void FileStat::update_atime() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    atime = tv.tv_sec;
}

namespace {

/* Compare two cache entries by the access time of the FileStat objects. */

bool cmp_by_atime(const StatsCache::cache_entry_t& a1,
        const StatsCache::cache_entry_t& a2) {
    return a1.second.get_atime() < a2.second.get_atime();
}

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
            mp3fs_debug("Removed out of date file '%s' from stats cache",
                    p->first.c_str());
            cache.erase(p);
        } else {
            mp3fs_debug("Found file '%s' in stats cache with size %u",
                    p->first.c_str(), file_stat.get_size());
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
        mp3fs_debug("Added file '%s' to stats cache with size %u",
                filename.c_str(), file_stat.get_size());
        cache.insert(std::make_pair(filename, file_stat));
    } else if (mtime >= p->second.get_mtime()) {
        mp3fs_debug("Updated file '%s' in stats cache with size %u",
                filename.c_str(), file_stat.get_size());
        p->second = file_stat;
    }
    check_size();
    pthread_mutex_unlock(&mutex);
}

/*
 * If the cache has exceeded the allotted size, prune invalid and old cache
 * entries until the cache is at 90% of capacity. Assumes the cache is locked.
 */
void StatsCache::check_size() {
    if (cache.size() <= params.statcachesize) {
        return;
    }

    mp3fs_debug("Pruning stats cache");
    size_t target_size = 9 * params.statcachesize / 10; // 90%
    std::vector<cache_entry_t> sorted_entries;

    /* First remove all invalid cache entries. */
    cache_t::iterator next_p;
    for (cache_t::iterator p = cache.begin(); p != cache.end(); p = next_p) {
        const std::string& decoded_file = p->first;
        const FileStat& file_stat = p->second;
        next_p = p;
        ++next_p;

        struct stat s;
        if (stat(decoded_file.c_str(), &s) < 0 ||
                s.st_mtime > file_stat.get_mtime()) {
            mp3fs_debug("Removed out of date file '%s' from stats cache",
                    p->first.c_str());
            errno = 0;
            cache.erase(p);
        } else {
            sorted_entries.push_back(*p);
        }
    }
    if (cache.size() <= target_size) {
        return;
    }

    // Sort all cache entries by the atime, and remove the oldest entries until
    // the cache size meets the target.
    sort(sorted_entries.begin(), sorted_entries.end(), cmp_by_atime);
    for (std::vector<cache_entry_t>::iterator p = sorted_entries.begin();
            p != sorted_entries.end() && cache.size() > target_size; ++p) {
        mp3fs_debug("Pruned oldest file '%s' from stats cache",
                p->first.c_str());
        cache.erase(p->first);
    }
}
