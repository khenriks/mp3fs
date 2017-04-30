/*
 * Ogg Vorbis decoder class source for mp3fs
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

#include "vorbis_decoder.h"

#include <algorithm>
#include <cstdlib>

#include <unistd.h>

#include "base64.h"
#include "picture.h"
#include "transcode.h"

namespace {

    /* Define invalid value for gain in decibels, to be used later. */
    const double INVALID_DB_GAIN = 1000.0;
}

/* Free the OggVorbis_File data structure and close the open Ogg Vorbis file
 * after the decoding process has finished.
 */
VorbisDecoder::~VorbisDecoder() {
    ov_clear(&vf);
    mp3fs_debug("Ogg Vorbis decoder: Closed.");
}

/*
 * Open the given Ogg Vorbis file and prepare for decoding. After this function,
 * the other methods can be used to process the file.
 */
int VorbisDecoder::open_file(const char* filename) {

    mp3fs_debug("Ogg Vorbis decoder: Initializing.");

    int fd = open(filename, 0);
    if (fd < 0) {
        mp3fs_debug("Ogg Vorbis decoder: open failed.");
        return -1;
    }

    struct stat s;
    if (fstat(fd, &s) < 0) {
        mp3fs_debug("Ogg Vorbis decoder: fstat failed.");
        close(fd);
        return -1;
    }
    mtime_ = s.st_mtime;

    FILE *file = fdopen(fd, "r");
    if (file == 0) {
        mp3fs_debug("Ogg Vorbis decoder: fdopen failed.");
        close(fd);
        return -1;
    }

    /* Initialise decoder */
    if (ov_open(file, &vf, NULL, 0) < 0) {
        mp3fs_debug("Ogg Vorbis decoder: Initialization failed.");
        fclose(file);
        return -1;
    }

    return 0;
}


time_t VorbisDecoder::mtime() {
    return mtime_;
}


/*
 * Process the metadata in the Ogg Vorbis file. This should be called at the
 * beginning, before reading audio data. The set_text_tag() and
 * set_picture_tag() methods of the given Encoder will be used to set the
 * metadata, with results going into the given Buffer. This function will also
 * read the actual PCM stream parameters.
 */
int VorbisDecoder::process_metadata(Encoder* encoder) {
    vorbis_comment *vc = NULL;

    if ((vi = ov_info(&vf, -1)) == NULL) {
        mp3fs_debug("Ogg Vorbis decoder: Failed to retrieve the file info.");
        return -1;
    }

    if (vi->channels > 2) {
        mp3fs_debug("Ogg Vorbis decoder: Only mono/stereo audio currently supported.");
        return -1;
    }

    if (encoder->set_stream_params(
            ov_pcm_total(&vf, -1),
            (int)vi->rate,
            vi->channels) == -1) {
        mp3fs_debug("Ogg Vorbis decoder: Failed to set encoder stream parameters.");
        return -1;
    }

    if ((vc = ov_comment(&vf, -1)) == NULL) {
        mp3fs_debug("Ogg Vorbis decoder: Failed to retrieve the Ogg Vorbis comment.");
        return -1;
    }

    double filegainref = 89.0;
    double dbgain = INVALID_DB_GAIN;

    for (int i = 0; i < vc->comments; ++i) {

        /*
         * Get the tagname - tagvalue pairs
         */
        std::string comment(vc->user_comments[i], vc->comment_lengths[i]);
        unsigned long delimiter_pos = comment.find_first_of('=');

        if ((delimiter_pos == 0) || (delimiter_pos >= comment.length() - 1)) {
            continue;
        }

        std::string tagname = comment.substr(0, delimiter_pos);
        std::string tagvalue = comment.substr(
                delimiter_pos + 1, comment.length() - delimiter_pos);

        /*
         * Normalize tag name to uppercase.
         */
        std::transform(tagname.begin(), tagname.end(),
                tagname.begin(), ::toupper);

        /*
         * Set the encoder's text tag if it's in the metatag_map, or else,
         * prepare the ReplayGain.
         */
        meta_map_t::const_iterator it = metatag_map.find(tagname);

        if (it != metatag_map.end()) {
            encoder->set_text_tag(it->second, tagvalue.c_str());
        }
        else if (tagname == "METADATA_BLOCK_PICTURE") {
            char* data;
            size_t data_len;
            base64_decode_alloc(tagvalue.c_str(), tagvalue.size(),
                                &data, &data_len);
            if (data == nullptr) {
                mp3fs_debug("Failed to decode METADATA_BLOCK_PICTURE; invalid "
                            "base64 or could not allocate memory.");
                return -1;
            }

            Picture picture({data, data + data_len});
            free(data);

            if (picture.decode()) {
                encoder->set_picture_tag(picture.get_mime_type(),
                    picture.get_type(),
                    picture.get_description(),
                    picture.get_data(),
                    picture.get_data_length());
            }
        }
        else if (tagname == "REPLAYGAIN_REFERENCE_LOUDNESS") {
            filegainref = atof(tagvalue.c_str());
        }
        else if (params.gainmode == 1
                && tagname == "REPLAYGAIN_ALBUM_GAIN") {
            dbgain = atof(tagvalue.c_str());
        }
        else if ((params.gainmode == 1 || params.gainmode == 2)
                && dbgain == INVALID_DB_GAIN
                && tagname == "REPLAYGAIN_TRACK_GAIN") {
            dbgain = atof(tagvalue.c_str());
        }
    }

    /*
     * Use the Replay Gain tag to set volume scaling. The appropriate
     * value for dbgain is set in the above if statements according to
     * the value of gainmode. Obey the gainref option here.
     */
    if (dbgain != INVALID_DB_GAIN) {
        encoder->set_gain_db(params.gainref - filegainref + dbgain);
    }

    return 0;
}

/*
 * Process a single frame of audio data. The encode_pcm_data() method
 * of the Encoder will be used to process the resulting audio data, with the
 * result going into the given Buffer.
 */
int VorbisDecoder::process_single_fr(Encoder* encoder, Buffer* buffer) {
    const int bigendian = 0;
    const int word = sizeof(short);
    const int signed_pcm = 1;
    const int decode_buf_size = 2 * word * 1024;

    union combining_buf {
        short as_short[decode_buf_size / sizeof(short)];
        char as_char[decode_buf_size];
    } decode_buffer;

    long read_bytes = ov_read(&vf, decode_buffer.as_char, decode_buf_size,
            bigendian, word, signed_pcm, &current_section);

    if (read_bytes > 0) {
        long total_samples = read_bytes / word;

        if (total_samples < 1) {
            mp3fs_debug("Ogg Vorbis decoder: Byte buffer contains less than word size.");
            return -1;
        }

        long samples_per_channel = total_samples / vi->channels;

        if (samples_per_channel < 1) {
            mp3fs_debug("Ogg Vorbis decoder: Not enough samples per channel.");
            return -1;
        }

        int32_t *encode_buffer[vi->channels];

        /* Mono/Stereo: 0 = left, 1 = right */
        for (int channel = 0; channel < vi->channels; ++channel) {
            encode_buffer[channel] = new int32_t[samples_per_channel];
        }

        for (long i = 0; i < samples_per_channel; ++i) {
            for (int channel = 0; channel < vi->channels; ++channel) {
                encode_buffer[channel][i] = decode_buffer.as_short[i * vi->channels + channel];
            }
        }

        /* Send integer buffer to encoder */
        if (encoder->encode_pcm_data(encode_buffer, (int)samples_per_channel,
                                 8 * word, *buffer) < 0) {

            mp3fs_debug("Ogg Vorbis decoder: Failed to encode integer buffer.");

            for (int channel = 0; channel < vi->channels; ++channel) {
                delete[] encode_buffer[channel];
                encode_buffer[channel] = NULL;
            }

            return -1;
        }

        for (int channel = 0; channel < vi->channels; ++channel) {
            delete[] encode_buffer[channel];
            encode_buffer[channel] = NULL;
        }

        return 0;
    }
    else if (read_bytes == 0) {
        mp3fs_debug("Ogg Vorbis decoder: Reached end of file.");
        return 1;
    }
    else {
        mp3fs_debug("Ogg Vorbis decoder: Failed to read file.");
        return -1;
    }
}

/*
 * This function creates the metadata tag map from FLAC values to the standard
 * values in the enum in coders.h. It will be called only once to set the
 * metatag_map static variable.
 */
const VorbisDecoder::meta_map_t VorbisDecoder::create_meta_map() {
    meta_map_t m;

    m["TITLE"] = METATAG_TITLE;
    m["ARTIST"] = METATAG_ARTIST;
    m["ALBUM"] = METATAG_ALBUM;
    m["GENRE"] = METATAG_GENRE;
    m["DATE"] = METATAG_DATE;
    m["COMPOSER"] = METATAG_COMPOSER;
    m["PERFORMER"] = METATAG_PERFORMER;
    m["COPYRIGHT"] = METATAG_COPYRIGHT;
    m["ENCODED_BY"] = METATAG_ENCODEDBY;
    m["ORGANIZATION"] = METATAG_ORGANIZATION;
    m["CONDUCTOR"] = METATAG_CONDUCTOR;
    m["ALBUMARTIST"] = METATAG_ALBUMARTIST;
    m["ALBUM ARTIST"] = METATAG_ALBUMARTIST;
    m["TRACKNUMBER"] = METATAG_TRACKNUMBER;
    m["TRACKTOTAL"] = METATAG_TRACKTOTAL;
    m["DISCNUMBER"] = METATAG_DISCNUMBER;
    m["DISCTOTAL"] = METATAG_DISCTOTAL;

    return m;
}

const VorbisDecoder::meta_map_t VorbisDecoder::metatag_map
    = VorbisDecoder::create_meta_map();
