/*
 * Ogg Vorbis decoder class header for mp3fs
 *
 * Copyright (C) 2015 Thomas Schwarzenberger
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

#ifndef MP3FS_CODECS_VORBIS_DECODER_H_
#define MP3FS_CODECS_VORBIS_DECODER_H_

#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include <ctime>
#include <map>
#include <string>

#include "codecs/coders.h"

class VorbisDecoder : public Decoder {
 public:
    ~VorbisDecoder() override;
    int open_file(const char* filename) override;
    time_t mtime() override;
    int process_metadata(Encoder* encoder) override;
    int process_single_fr(Encoder* encoder) override;

 private:
    time_t mtime_;
    OggVorbis_File vf;
    vorbis_info* vi;
    int current_section;
    using meta_map_t = std::map<std::string, int>;
    static const meta_map_t metatag_map;
};

#endif  // MP3FS_CODECS_VORBIS_DECODER_H_
