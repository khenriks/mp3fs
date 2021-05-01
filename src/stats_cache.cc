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

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <iterator>
#include <ostream>
#include <vector>

#include "logging.h"
#include "mp3fs.h"

/*
 * Get the file size from the cache for the given filename, if it exists.
 * Use 'mtime' as the modified time of the file to check for an invalid cache
 * entry. Return true if the file size was found.
 */
bool StatsCache::get_filesize(const std::string& filename, time_t mtime,
                              size_t* filesize) {
    std::lock_guard<std::mutex> l(mutex_);
    auto it = cache_.find(filename);
    if (it != cache_.end()) {
        FileStat& file_stat = it->second;
        if (mtime > file_stat.get_mtime()) {
            // The decoded file has changed since this entry was created, so
            // remove the invalid entry.
            Log(DEBUG) << "Removed out of date file '" << it->first
                       << "' from stats cache";
            cache_.erase(it);
        } else {
            Log(DEBUG) << "Found file '" << it->first
                       << "' in stats cache with size " << file_stat.get_size();
            *filesize = file_stat.get_size();
            file_stat.update_atime();
            return true;
        }
    }
    return false;
}

/* Add or update an entry in the stats cache */

void StatsCache::put_filesize(const std::string& filename, size_t filesize,
                              time_t mtime) {
    const FileStat file_stat(filesize, mtime);
    std::unique_lock<std::mutex> l(mutex_);
    auto it = cache_.find(filename);
    if (it == cache_.end()) {
        Log(DEBUG) << "Added file '" << filename
                   << "' to stats cache with size " << file_stat.get_size();
        cache_.insert(std::make_pair(filename, file_stat));
    } else if (mtime >= it->second.get_mtime()) {
        Log(DEBUG) << "Updated file '" << filename
                   << "' in stats cache with size " << file_stat.get_size();
        it->second = file_stat;
    }
    if (cache_.size() > params.statcachesize) {
        // Don't hold lock when calling prune.
        l.unlock();
        prune();
    }
}

/*
 * Prune invalid and old cache entries until the cache is at 90% of capacity.
 */
void StatsCache::prune() {
    Log(DEBUG) << "Pruning stats cache";
    const size_t target_size = 9 * params.statcachesize / 10;  // 90% NOLINT

    /* Copy all the entries to a vector to be sorted. */
    std::vector<cache_entry_t> sorted_entries;
    {
        std::lock_guard<std::mutex> l(mutex_);
        std::copy(cache_.begin(), cache_.end(),
                  std::back_inserter(sorted_entries));
    }
    /* Sort the entries by access time, with the oldest first */
    std::sort(sorted_entries.begin(), sorted_entries.end(),
              [](const cache_entry_t& a, const cache_entry_t& b) {
                  return a.second.get_atime() < b.second.get_atime();
              });

    /*
     * Remove all invalid cache entries. Don't bother removing invalid
     * entries from sorted_entries, as the removal from a vector can have bad
     * performance and removing the entry twice (once here and once in the next
     * loop) is harmless.
     *
     * Lock the cache for each entry removed instead of putting the lock around
     * the entire loop because the stat() can be expensive.
     */
    for (const auto& e : sorted_entries) {
        const std::string& decoded_file = e.first;
        const FileStat& file_stat = e.second;
        struct stat s = {};
        if (stat(decoded_file.c_str(), &s) < 0 ||
            s.st_mtime > file_stat.get_mtime()) {
            Log(DEBUG) << "Removed out of date file '" << decoded_file
                       << "' from stats cache";
            errno = 0;
            std::lock_guard<std::mutex> l(mutex_);
            remove_entry(decoded_file, file_stat);
        }
    }

    /* Remove the oldest entries until the cache size meets the target. */
    std::lock_guard<std::mutex> l(mutex_);
    for (const auto& e : sorted_entries) {
        if (cache_.size() <= target_size) {
            break;
        }
        Log(DEBUG) << "Pruned oldest file '" << e.first << "' from stats cache";
        remove_entry(e.first, e.second);
    }
}

/*
 * Remove the cache entry if it exists and if the entry's file stat matches the
 * given file stat, i.e. the file stat hasn't changed.  Assumes the cache is
 * locked.
 */
void StatsCache::remove_entry(const std::string& file,
                              const FileStat& file_stat) {
    auto it = cache_.find(file);
    if (it != cache_.end() && it->second == file_stat) {
        cache_.erase(it);
    }
}

StatsCache::FileStat::FileStat(size_t size, time_t mtime)
    : size_(size), mtime_(mtime) {
    update_atime();
}

void StatsCache::FileStat::update_atime() {
    atime_ = time(nullptr);
}

bool StatsCache::FileStat::operator==(const FileStat& other) const {
    return size_ == other.size_ && atime_ == other.atime_ &&
           mtime_ == other.mtime_;
}
