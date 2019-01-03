/*
 * Path translation routines for MP3FS
 *
 * Copyright (C) 2018 K. Henriksson
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

#ifndef MP3FS_PATH_H
#define MP3FS_PATH_H

#include <ostream>
#include <string>

class Path {
public:
    /** Construct a Path from a relative path inside the mp3fs mount. */
    static Path FromMp3fsRelative(const char* path) { return Path(path); }

    /**
     * Return source path for normal files.
     *
     * This returns the path in the source directory assuming no change in
     * extension.
     */
    std::string NormalSource() const;

    /**
     * Return source path for transcoded files.
     *
     * If the original filename ends in the destination format extension,
     * this will check for the existence of a file with one of the supported
     * decoder extensions. If none of those exist, the same value as
     * NormalSource will be returned.
     */
    std::string TranscodeSource() const;

    friend std::ostream& operator<<(std::ostream&, const Path&);

private:
    Path(const std::string& relative_path) : relative_path_(relative_path) {}

    std::string relative_path_;
};

#endif  // MP3FS_PATH_H
