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

#include <dirent.h>

#include <cstddef>
#include <memory>
#include <string>

#include "codecs/coders.h"
#include "mp3fs.h"

std::string Path::normal_source() const {
    return std::string(params.basepath) + relative_path_;
}

std::string Path::transcode_source() const {
    const std::string source = normal_source();
    const size_t dot_idx = source.rfind('.');
    const size_t slash_idx = source.rfind('/');
    const std::string source_dir = source.substr(0, slash_idx);

    if (dot_idx != std::string::npos &&
        source.substr(dot_idx + 1) == params.desttype) {
        const std::string source_base =
            source.substr(slash_idx + 1, dot_idx - slash_idx);
        const std::unique_ptr<DIR, decltype(&closedir)> dp(
            opendir(source_dir.c_str()), closedir);
        while (struct dirent* de = readdir(dp.get())) {
            const std::string de_name = de->d_name;
            if (de_name.find(source_base) == 0) {
                const std::string de_ext =
                    de_name.substr(de_name.rfind('.') + 1);
                if (Decoder::CreateDecoder(de_ext) != nullptr) {
                    /* This is a valid transcode source file. */
                    return source_dir + "/" + de_name;
                }
            }
        }
    }

    return source;
}

std::ostream& operator<<(std::ostream& ostream, const Path& path) {
    return ostream << path.relative_path_;
}
