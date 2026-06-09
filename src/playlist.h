/*
 * Playlist file reader for MP3FS
 *
 * Copyright (C) 2026 Isaac Morales
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

#ifndef MP3FS_PLAYLIST_H_
#define MP3FS_PLAYLIST_H_

#include <string>

#include "reader.h"

/** Return true if path refers to a supported playlist format. */
bool is_playlist(const std::string& path);

/**
 * Reader that serves a playlist file with source audio extensions translated
 * to the transcoded destination extension.
 *
 * The entire translated content is materialised in memory on construction
 * (playlist files are small) so that size reporting and random access both
 * work without re-reading the source file.
 */
class PlaylistReader : public Reader {
 public:
    explicit PlaylistReader(const std::string& source_path);

    ssize_t read(char* buff, off_t offset, size_t len) override;

    size_t size() const { return content_.size(); }

 private:
    std::string content_;
};

#endif  // MP3FS_PLAYLIST_H_
