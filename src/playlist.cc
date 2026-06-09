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

#include "playlist.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "codecs/coders.h"
#include "mp3fs.h"

namespace {

std::string file_extension(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    return path.substr(dot + 1);
}

/* Translate a single path token: replace a decodable extension with desttype. */
std::string convert_extension(const std::string& path) {
    const size_t ext_pos = path.rfind('.');
    if (ext_pos != std::string::npos &&
        Decoder::CreateDecoder(path.substr(ext_pos + 1)) != nullptr) {
        return path.substr(0, ext_pos + 1) + params.desttype;
    }
    return path;
}

/*
 * Translate an M3U / M3U8 stream.
 * Lines beginning with '#' are directives and are passed through unchanged.
 * All other non-empty lines are treated as file paths.
 */
std::string translate_m3u(std::istream& in) {
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        /* Strip Windows-style carriage return. */
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            line = convert_extension(line);
        }
        out << line << '\n';
    }
    return out.str();
}

/*
 * Translate a PLS stream.
 * Only "File<N>=<path>" entries reference audio files; everything else
 * (Title, Length, NumberOfEntries, Version, section headers) is passed
 * through unchanged.
 */
std::string translate_pls(std::istream& in) {
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        /* Case-insensitive check for "File" prefix followed by digits and '='. */
        if (line.size() >= 6) {
            std::string lower = line.substr(0, 4);
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower == "file") {
                const size_t eq = line.find('=');
                if (eq != std::string::npos) {
                    /* Verify the characters between "File" and '=' are digits. */
                    bool all_digits = true;
                    for (size_t i = 4; i < eq; ++i) {
                        if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
                            all_digits = false;
                            break;
                        }
                    }
                    if (all_digits && eq > 4) {
                        const std::string value = line.substr(eq + 1);
                        out << line.substr(0, eq + 1) << convert_extension(value)
                            << '\n';
                        continue;
                    }
                }
            }
        }
        out << line << '\n';
    }
    return out.str();
}

std::string translate_playlist(const std::string& source_path) {
    std::ifstream f(source_path);
    if (!f) return {};
    const std::string ext = file_extension(source_path);
    if (ext == "pls") {
        return translate_pls(f);
    }
    return translate_m3u(f);
}

}  // namespace

bool is_playlist(const std::string& path) {
    const std::string ext = file_extension(path);
    return ext == "m3u" || ext == "m3u8" || ext == "pls";
}

PlaylistReader::PlaylistReader(const std::string& source_path)
    : content_(translate_playlist(source_path)) {}

ssize_t PlaylistReader::read(char* buff, off_t offset, size_t len) {
    if (offset < 0 || static_cast<size_t>(offset) >= content_.size()) {
        return 0;
    }
    const size_t available = content_.size() - static_cast<size_t>(offset);
    const size_t to_copy = std::min(len, available);
    content_.copy(buff, to_copy, static_cast<size_t>(offset));
    return static_cast<ssize_t>(to_copy);
}
