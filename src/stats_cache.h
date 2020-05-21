/*
 * File stats cache interface for mp3fs
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

#ifndef MP3FS_STATS_CACHE_H_
#define MP3FS_STATS_CACHE_H_

#include <pthread.h>

#include <ctime>
#include <map>
#include <string>
#include <utility>

class StatsCache {
 public:
    StatsCache() : mutex_(PTHREAD_MUTEX_INITIALIZER) {}
    ~StatsCache() { pthread_mutex_destroy(&mutex_); }
    StatsCache(const StatsCache&) = delete;
    StatsCache& operator=(const StatsCache&) = delete;

    bool get_filesize(const std::string& filename, time_t mtime,
                      size_t* filesize);
    void put_filesize(const std::string& filename, size_t filesize,
                      time_t mtime);

 private:
    /* Holds the size and modified time for a file. */
    class FileStat {
     public:
        FileStat(size_t size, time_t mtime);

        void update_atime();

        size_t get_size() const { return size_; }
        time_t get_atime() const { return atime_; }
        time_t get_mtime() const { return mtime_; }
        bool operator==(const FileStat& other) const;

     private:
        size_t size_;
        // The last time this object was accessed. Used to implement the most
        // recently used cache policy.
        time_t atime_ = 0;
        // The modified time of the decoded file when the size was computed.
        time_t mtime_;
    };

    using cache_entry_t = std::pair<std::string, FileStat>;

    static bool cmp_by_atime(const cache_entry_t& a1, const cache_entry_t& a2);

    void prune();
    void remove_entry(const std::string& file, const FileStat& file_stat);

    std::map<std::string, FileStat> cache_;
    pthread_mutex_t mutex_;
};

#endif  // MP3FS_STATS_CACHE_H_
