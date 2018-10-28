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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "codecs/picture.h"
#include "lib/base64.h"
#include "logging.h"

/* Free the OggVorbis_File data structure and close the open Ogg Vorbis file
 * after the decoding process has finished.
 */
VorbisDecoder::~VorbisDecoder() {
    ov_clear(&vf);
    Log(DEBUG) << "Ogg Vorbis decoder: Closed.";
}

/*
 * Open the given Ogg Vorbis file and prepare for decoding. After this function,
 * the other methods can be used to process the file.
 */
int VorbisDecoder::open_file(const char* filename) {

    Log(DEBUG) << "Ogg Vorbis decoder: Initializing.";

    int fd = open(filename, 0);
    if (fd < 0) {
        Log(ERROR) << "Ogg Vorbis decoder: open failed.";
        return -1;
    }

    struct stat s;
    if (fstat(fd, &s) < 0) {
        Log(ERROR) << "Ogg Vorbis decoder: fstat failed.";
        close(fd);
        return -1;
    }
    mtime_ = s.st_mtime;

    FILE *file = fdopen(fd, "r");
    if (file == 0) {
        Log(ERROR) << "Ogg Vorbis decoder: fdopen failed.";
        close(fd);
        return -1;
    }

    /* Initialise decoder */
    if (ov_open(file, &vf, NULL, 0) < 0) {
        Log(ERROR) << "Ogg Vorbis decoder: Initialization failed.";
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
        Log(ERROR) << "Ogg Vorbis decoder: Failed to retrieve the file info.";
        return -1;
    }

    if (vi->channels > 2) {
        Log(ERROR) << "Ogg Vorbis decoder: Only mono/stereo audio currently supported.";
        return -1;
    }

    if (encoder->set_stream_params(
            ov_pcm_total(&vf, -1),
            (int)vi->rate,
            vi->channels) == -1) {
        Log(ERROR) << "Ogg Vorbis decoder: Failed to set encoder stream parameters.";
        return -1;
    }

    if ((vc = ov_comment(&vf, -1)) == NULL) {
        Log(ERROR) << "Ogg Vorbis decoder: Failed to retrieve the Ogg Vorbis comment.";
        return -1;
    }

    double gainref = Encoder::invalid_db,
        album_gain = Encoder::invalid_db,
        track_gain = Encoder::invalid_db;

    for (int i = 0; i < vc->comments; ++i) {

        /*
         * Get the tagname - tagvalue pairs
         */
        std::string comment(vc->user_comments[i], vc->comment_lengths[i]);
        size_t delimiter_pos = comment.find_first_of('=');

        if (delimiter_pos == 0 || delimiter_pos == std::string::npos) {
            continue;
        }

        std::string tagname = comment.substr(0, delimiter_pos);
        std::string tagvalue = comment.substr(delimiter_pos + 1);

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
                Log(ERROR) <<
                        "Failed to decode METADATA_BLOCK_PICTURE; invalid "
                        "base64 or could not allocate memory.";
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
            gainref = atof(tagvalue.c_str());
        }
        else if (tagname == "REPLAYGAIN_ALBUM_GAIN") {
            album_gain = atof(tagvalue.c_str());
        }
        else if (tagname == "REPLAYGAIN_TRACK_GAIN") {
            track_gain = atof(tagvalue.c_str());
        }
    }

    encoder->set_gain(gainref, album_gain, track_gain);

    return 0;
}

/*
 * Process a single frame of audio data. The encode_pcm_data() method
 * of the Encoder will be used to process the resulting audio data, with the
 * result going into the given Buffer.
 */
int VorbisDecoder::process_single_fr(Encoder* encoder) {
    std::vector<int16_t> decode_buffer(2048);

    long read_bytes = ov_read(&vf, (char*)decode_buffer.data(),
                              (int)(2 * decode_buffer.size()),
                              0, 2, 1, &current_section);

    long total_samples = read_bytes / 2;

    if (total_samples > 0) {
        long samples_per_channel = total_samples / vi->channels;

        if (samples_per_channel < 1) {
            Log(ERROR) << "Ogg Vorbis decoder: Not enough samples per channel.";
            return -1;
        }

        std::vector<std::vector<int32_t>>
            encode_buffer(vi->channels, std::vector<int32_t>(samples_per_channel));
        int32_t* encode_buffer_ptr[vi->channels];

        for (int channel = 0; channel < vi->channels; ++channel) {
            encode_buffer_ptr[channel] = encode_buffer[channel].data();
            for (long i = 0; i < samples_per_channel; ++i) {
                encode_buffer[channel][i] = decode_buffer[i * vi->channels + channel];
            }
        }

        /* Send integer buffer to encoder */
        if (encoder->encode_pcm_data(encode_buffer_ptr,
                                     (int)samples_per_channel, 16) < 0) {
            Log(ERROR) << "Ogg Vorbis decoder: Failed to encode integer buffer.";

            return -1;
        }

        return 0;
    }
    else if (total_samples == 0) {
        Log(DEBUG) << "Ogg Vorbis decoder: Reached end of file.";
        return 1;
    }
    else {
        Log(ERROR) << "Ogg Vorbis decoder: Failed to read file.";
        return -1;
    }
}

const VorbisDecoder::meta_map_t VorbisDecoder::metatag_map = {
    {"TITLE", METATAG_TITLE},
    {"ARTIST", METATAG_ARTIST},
    {"ALBUM", METATAG_ALBUM},
    {"GENRE", METATAG_GENRE},
    {"DATE", METATAG_DATE},
    {"COMPOSER", METATAG_COMPOSER},
    {"PERFORMER", METATAG_PERFORMER},
    {"COPYRIGHT", METATAG_COPYRIGHT},
    {"ENCODED_BY", METATAG_ENCODEDBY},
    {"ORGANIZATION", METATAG_ORGANIZATION},
    {"CONDUCTOR", METATAG_CONDUCTOR},
    {"ALBUMARTIST", METATAG_ALBUMARTIST},
    {"ALBUM ARTIST", METATAG_ALBUMARTIST},
    {"TRACKNUMBER", METATAG_TRACKNUMBER},
    {"TRACKTOTAL", METATAG_TRACKTOTAL},
    {"DISCNUMBER", METATAG_DISCNUMBER},
    {"DISCTOTAL", METATAG_DISCTOTAL},
};
