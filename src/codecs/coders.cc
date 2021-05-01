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

#include <algorithm>
#include <cctype>
#include <memory>

/*
 * Conditionally include specific encoders and decoders based on
 * configuration.
 */
#ifdef HAVE_MP3
#include <lame/lame.h>

#include "codecs/mp3_encoder.h"
#endif
#ifdef HAVE_FLAC
#include <FLAC/format.h>

#include "codecs/flac_decoder.h"
#endif
#ifdef HAVE_VORBIS
#include <vorbis/codec.h>

#include "codecs/vorbis_decoder.h"
#endif

#include "mp3fs.h"

namespace {
constexpr double kDefaultGain = 89.0;
}

void Encoder::set_gain(double gainref, double album_gain, double track_gain) {
    if (gainref == kInvalidDb) {
        gainref = kDefaultGain;
    }

    double dbgain = kInvalidDb;
    if (params.gainmode == 1 && album_gain != kInvalidDb) {
        dbgain = album_gain;
    } else if ((params.gainmode == 1 || params.gainmode == 2) &&
               track_gain != kInvalidDb) {
        dbgain = track_gain;
    }

    /*
     * Use the Replay Gain tag to set volume scaling. The appropriate
     * value for dbgain is set in the above if statements according to
     * the value of gainmode. Obey the gainref option here.
     */
    if (dbgain != kInvalidDb) {
        set_gain_db(params.gainref - gainref + dbgain);
    }
}

/* Create instance of class derived from Encoder. */
std::unique_ptr<Encoder> Encoder::CreateEncoder(const std::string& file_type,
                                                Buffer* buffer) {
#ifdef HAVE_MP3
    if (file_type == "mp3") {
        return std::unique_ptr<Encoder>(new Mp3Encoder(buffer));
    }
#endif
    return nullptr;
}

/* Create instance of class derived from Decoder. */
std::unique_ptr<Decoder> Decoder::CreateDecoder(std::string file_type) {
    // Convert file type to lowercase.
    std::transform(file_type.begin(), file_type.end(), file_type.begin(),
                   [](unsigned char c) { return std::tolower(c); });
#ifdef HAVE_FLAC
    if (file_type == "flac") {
        return std::unique_ptr<Decoder>(new FlacDecoder());
    }
#endif
#ifdef HAVE_VORBIS
    if (file_type == "ogg" || file_type == "oga") {
        return std::unique_ptr<Decoder>(new VorbisDecoder());
    }
#endif
    return nullptr;
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
