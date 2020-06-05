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

#include "path.h"

#include <glob.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "codecs/coders.h"
#include "mp3fs.h"

namespace {

// Simple C++ wrapper for glob.
std::vector<std::string> glob_match(const std::string& pattern) {
    std::vector<std::string> result;

    glob_t globbuf;
    if (glob(pattern.c_str(), 0, nullptr, &globbuf) == 0) {
        std::copy(globbuf.gl_pathv, globbuf.gl_pathv + globbuf.gl_pathc,
                  std::back_inserter(result));
    }

    globfree(&globbuf);
    return result;
}

}  // namespace

std::string Path::NormalSource() const {
    return std::string(params.basepath) + relative_path_;
}

std::string Path::TranscodeSource() const {
    const std::string source = NormalSource();
    const size_t dot_idx = source.rfind('.');

    if (dot_idx != std::string::npos &&
        source.substr(dot_idx + 1) == params.desttype) {
        const std::string source_base = source.substr(0, dot_idx);
        for (const std::string& candidate : glob_match(source_base + ".*")) {
            const std::string candidate_ext =
                candidate.substr(candidate.rfind('.') + 1);

            if (Decoder::CreateDecoder(candidate_ext) != nullptr) {
                /* This is a valid transcode source file. */
                return candidate;
            }
        }
    }

    return source;
}

std::ostream& operator<<(std::ostream& ostream, const Path& path) {
    return ostream << path.relative_path_;
}
