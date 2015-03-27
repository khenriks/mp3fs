/*
 * Encoder and Decoder class source for mp3fs
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

#include "coders.h"

#include "transcode.h"

/*
 * Conditionally include specific encoders and decoders based on
 * configuration.
 */
#ifdef HAVE_MP3
#include "mp3_encoder.h"
#endif
#ifdef HAVE_FLAC
#include "flac_decoder.h"
#endif
#ifdef HAVE_VORBIS
#include "vorbis_decoder.h"
#endif

/* Create instance of class derived from Encoder. */
Encoder* Encoder::CreateEncoder(std::string file_type) {
#ifdef HAVE_MP3
    if (file_type == "mp3") return new Mp3Encoder();
#endif
    return NULL;
}

/* Create instance of class derived from Decoder. */
Decoder* Decoder::CreateDecoder(std::string file_type) {
#ifdef HAVE_FLAC
    if (file_type == "flac") return new FlacDecoder();
#endif
#ifdef HAVE_VORBIS
    if (file_type == "ogg" || file_type == "oga") return new VorbisDecoder();
#endif
    return NULL;
}

/* Define list of available encoder extensions. */
const char* encoder_list[] = {
#ifdef HAVE_MP3
    "mp3",
#endif
};

const size_t encoder_list_len = sizeof(encoder_list)/sizeof(const char*);

/* Define list of available decoder extensions. */
const char* decoder_list[] = {
#ifdef HAVE_FLAC
    "flac",
#endif
#ifdef HAVE_VORBIS
    "ogg",
    "oga",
#endif
};

const size_t decoder_list_len = sizeof(decoder_list)/sizeof(const char*);

/* Use "C" linkage to allow access from C code. */
extern "C" {

    /* Check if an encoder is available to encode to the specified type. */
    int check_encoder(const char* type) {
        Encoder* enc = Encoder::CreateEncoder(type);
        if (enc) {
            delete enc;
            return 1;
        } else {
            return 0;
        }
    }

    /* Check if a decoder is available to decode from the specified type. */
    int check_decoder(const char* type) {
        Decoder* dec = Decoder::CreateDecoder(type);
        if (dec) {
            delete dec;
            return 1;
        } else {
            return 0;
        }
    }

}
