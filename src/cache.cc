/*
 * Cache controller class for mp3fs
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

#include "cache.h"
#include "cache_entry.h"

#include "transcode.h"

Cache::Cache()
{
}

Cache::~Cache() {
    // Clean up memory
    for (cache_t::iterator p = cache.begin(); p != cache.end(); ++p) {
        Cache_Entry *cache_entry = p->second;
        delete cache_entry;
    }

    cache.clear();
}

Cache_Entry* Cache::open(const char *filename)
{
    Cache_Entry* cache_entry = NULL;

    cache_t::iterator p = cache.find(filename);
    if (p == cache.end()) {
        mp3fs_debug("Created new transcoder for file '%s'.", filename);
        cache_entry = new Cache_Entry(filename);
        if (cache_entry == NULL)
        {
            mp3fs_error("Out of memory for file '%s'.", filename);
            return NULL;
        }
        cache.insert(std::make_pair(filename, cache_entry));
    } else {
        mp3fs_debug("Reusing cached transcoder for file '%s'.", filename);
        cache_entry = p->second;
    }

    return cache_entry;
}

void Cache::close(Cache_Entry **cache_entry, bool erase_cache)
{
    if ((*cache_entry) == NULL)
    {
        return;
    }

    std::string filename((*cache_entry)->m_filename);
    if ((*cache_entry)->m_info.m_error || (!(*cache_entry)->m_is_decoding && !(*cache_entry)->m_info.m_finished))
    {
        cache_t::iterator p = cache.find(filename);
        if (p == cache.end()) {
            mp3fs_warning("Unknown transcoder for file '%s', unable to delete.", filename.c_str());
        } else {
            mp3fs_debug("Deleted transcoder for file '%s'.", filename.c_str());
            cache.erase(p);
        }

        (*cache_entry)->close(erase_cache);

        delete *cache_entry;
        *cache_entry = NULL;
    }
    else
    {
        //(*cache_entry)->flush();
        (*cache_entry)->close();

        mp3fs_debug("Keeping transcoder for file '%s'.", filename.c_str());
    }
}
