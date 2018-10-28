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

#ifndef VORBIS_DECODER_H
#define VORBIS_DECODER_H

#include <map>
#include <string>

#include <vorbis/vorbisfile.h>

#include "codecs/coders.h"

class VorbisDecoder : public Decoder {
public:
    ~VorbisDecoder();
    int open_file(const char* filename);
    time_t mtime();
    int process_metadata(Encoder* encoder);
    int process_single_fr(Encoder* encoder);
private:
    time_t mtime_;
    OggVorbis_File vf;
    vorbis_info *vi;
    int current_section;
    typedef std::map<std::string,int> meta_map_t;
    static const meta_map_t metatag_map;
};


#endif
