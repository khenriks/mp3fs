/*
 * data buffer class header for mp3fs
 *
 * Copyright (C) 2013 K. Henriksson
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

#ifndef CACHE_H
#define CACHE_H

#pragma once

//#include <stdint.h>

#include <cstddef>
#include <string>
#include <map>

class Cache_Entry;

class Cache {
    typedef std::map<std::string, Cache_Entry *> cache_t;

public:
    Cache();
    ~Cache();

    Cache_Entry *open(const char *filename);
    void close(Cache_Entry **cache_entry, bool erase_cache = false);

private:
    cache_t cache;
};

#endif
