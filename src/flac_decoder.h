/*
 * FLAC decoder class header for mp3fs
 *
 * Copyright (C) 2013 Kristofer Henriksson
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

#include "coders.h"

#include <map>
#include <string>

#include <FLAC++/decoder.h>
#include <FLAC++/metadata.h>

class FlacDecoder : public Decoder, private FLAC::Decoder::File {
public:
    int open_file(const char* filename);
    int process_metadata(Encoder* encoder, Buffer* buffer);
    int process_single_fr(Encoder* encoder, Buffer* buffer);
protected:
    FLAC__StreamDecoderWriteStatus write_callback(const FLAC__Frame* frame,
                                                  const FLAC__int32* const buffer[]);
    void metadata_callback(const FLAC__StreamMetadata* metadata);
    void error_callback(FLAC__StreamDecoderErrorStatus status);
private:
    Encoder* encoder_c;
    Buffer* buffer_c;
    FLAC::Metadata::StreamInfo info;
    typedef std::map<std::string,int> meta_map_t;
    static const meta_map_t create_meta_map();
    static const meta_map_t metatag_map;
};


#endif
