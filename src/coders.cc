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

#include <string.h>

#if 0

// ??? FÜR SPÄTER!

#include "ffmpeg_decoder.h"
#include "ffmpeg_encoder.h"

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
#endif

/* Define list of available encoder extensions. */
const char* encoder_list[] = {
    "mp3",
    "mp4",
};

const size_t encoder_list_len = sizeof(encoder_list)/sizeof(const char*);

/* Define list of available decoder extensions. */
const char* decoder_list[] = {
    "flac",
    "ogg",
    "oga",
    "ogv",
    "mp4",
    "m4a",
    "m4v",
    "webm",
    "flv",
    "mpg",
    "ts",
};

const size_t decoder_list_len = sizeof(decoder_list)/sizeof(const char*);

/* Use "C" linkage to allow access from C code. */
extern "C" {

/* Init en/decoder lists. */
void init_coders() {

    //     /* Define list of available decoder extensions. */
    //         decoder_list.push_back("flac");
    //         decoder_list.push_back("ogg");
    //         decoder_list.push_back("oga");
    //         decoder_list.push_back("ogv");
    //         decoder_list.push_back("mp4");
    //         decoder_list.push_back("m4a");
    //         decoder_list.push_back("m4v");
    //         decoder_list.push_back("webm");
    //         decoder_list.push_back("flv");
    //         decoder_list.push_back("mpg");
    //         decoder_list.push_back("ts");
}

/* Check if an encoder is available to encode to the specified type. */
int check_encoder(const char* type) {
    int found = 0;

    for (int n = 0; encoder_list[n]; n++)
    {
        if (!strcasecmp(type, encoder_list[n]))
        {
            found = -1;
            break;
        }
    }
    return found;
}

/* Check if a decoder is available to decode from the specified type. */
int check_decoder(const char* type) {
    int found = 0;

    for (int n = 0; decoder_list[n]; n++)
    {
        if (!strcasecmp(type, decoder_list[n]))
        {
            found = -1;
            break;
        }
    }
    return found;
}

}
