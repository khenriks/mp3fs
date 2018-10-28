/*
 * FLAC decoder class header for mp3fs
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

#ifndef FLAC_DECODER_H
#define FLAC_DECODER_H

#include <map>
#include <string>

// The pragmas suppress the named warning from FLAC++, on both GCC and clang.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#include <FLAC++/decoder.h>
#include <FLAC++/metadata.h>
#pragma GCC diagnostic pop

#include "codecs/coders.h"

class FlacDecoder : public Decoder, private FLAC::Decoder::File {
public:
    FlacDecoder() : has_streaminfo(false) {};
    int open_file(const char* filename);
    time_t mtime();
    int process_metadata(Encoder* encoder);
    int process_single_fr(Encoder* encoder);
protected:
    FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[]);
    void metadata_callback(const FLAC__StreamMetadata* metadata);
    void error_callback(FLAC__StreamDecoderErrorStatus status);
private:
    Encoder* encoder_c;
    time_t mtime_;
    FLAC::Metadata::StreamInfo info;
    bool has_streaminfo;
    typedef std::map<std::string,int> meta_map_t;
    static const meta_map_t metatag_map;
};


#endif
