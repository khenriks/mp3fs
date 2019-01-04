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

#include "codecs/coders.h"

#include "transcode.h"

/*
 * Conditionally include specific encoders and decoders based on
 * configuration.
 */
#ifdef HAVE_MP3
#include "codecs/mp3_encoder.h"
#endif
#ifdef HAVE_FLAC
#include "codecs/flac_decoder.h"
#endif
#ifdef HAVE_VORBIS
#include "codecs/vorbis_decoder.h"
#endif

void Encoder::set_gain(double gainref, double album_gain, double track_gain) {
    if (gainref == invalid_db) {
        gainref = 89.0;
    }

    double dbgain = invalid_db;
    if (params.gainmode == 1 && album_gain != invalid_db) {
        dbgain = album_gain;
    } else if ((params.gainmode == 1 || params.gainmode == 2) &&
               track_gain != invalid_db) {
        dbgain = track_gain;
    }

    /*
     * Use the Replay Gain tag to set volume scaling. The appropriate
     * value for dbgain is set in the above if statements according to
     * the value of gainmode. Obey the gainref option here.
     */
    if (dbgain != invalid_db) {
        set_gain_db(params.gainref - gainref + dbgain);
    }
}

/* Create instance of class derived from Encoder. */
Encoder* Encoder::CreateEncoder(std::string file_type, Buffer& buffer,
                                size_t actual_size) {
#ifdef HAVE_MP3
    if (file_type == "mp3") return new Mp3Encoder(buffer, actual_size);
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
const std::vector<std::string> encoder_list = {
#ifdef HAVE_MP3
    "mp3",
#endif
};

/* Define list of available decoder extensions. */
const std::vector<std::string> decoder_list = {
#ifdef HAVE_FLAC
    "flac",
#endif
#ifdef HAVE_VORBIS
    "ogg",
    "oga",
#endif
};

/* Check if an encoder is available to encode to the specified type. */
int check_encoder(const char* type) {
    Buffer b;
    Encoder* enc = Encoder::CreateEncoder(type, b);
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

void print_codec_versions(std::ostream& out) {
#ifdef HAVE_MP3
    out << "LAME library version: " << get_lame_version() << std::endl;
#endif
#ifdef HAVE_FLAC
    out << "FLAC library version: " << FLAC__VERSION_STRING << std::endl;
#endif
#ifdef HAVE_VORBIS
    out << vorbis_version_string() << std::endl;
#endif
}
