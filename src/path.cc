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

#include "codecs/coders.h"
#include "mp3fs.h"
#include "path.h"

#include <unistd.h>

#include <string>

std::string Path::NormalSource() const {
    return std::string(params.basepath) + relative_path_;
}

std::string Path::TranscodeSource() const {
    const std::string base = NormalSource();
    const size_t dot = base.rfind('.');

    if (dot != std::string::npos && base.substr(dot + 1) == params.desttype) {
        for (const auto& dec : decoder_list) {
            std::string candidate = base.substr(0, dot + 1) + dec;
            if (access(candidate.c_str(), F_OK) == 0) {
                /* File exists with this extension */
                return candidate;
            } else {
                /* File does not exist; not an error */
                errno = 0;
            }
        }
    }

    return base;
}

std::ostream& operator<<(std::ostream& ostream, const Path& path) {
    return ostream << path.relative_path_;
}
