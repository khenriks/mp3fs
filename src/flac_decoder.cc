/*
 * FLAC decoder class source for mp3fs
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

#include "flac_decoder.h"

#include <algorithm>

#include "transcode.h"

namespace {
    /* Define invalid value for gain in decibels, to be used later. */
    const float INVALID_DB_GAIN = 1000.0;
}

/*
 * Open the given FLAC file and prepare for decoding. After this function,
 * the other methods can be used to process the file.
 */
int FlacDecoder::open_file(const char* filename) {
    /*
     * The metadata response types must be set before the decoder is
     * initialized.
     */
    set_metadata_respond(FLAC__METADATA_TYPE_VORBIS_COMMENT);
    set_metadata_respond(FLAC__METADATA_TYPE_PICTURE);

    mp3fs_debug("FLAC ready to initialize.");

    /* Initialise decoder */
    if (init(filename) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        mp3fs_debug("FLAC init failed.");
        return -1;
    }

    mp3fs_debug("FLAC initialized successfully.");

    return 0;
}

/*
 * Process the metadata in the FLAC file. This should be called at the
 * beginning, before reading audio data. The set_text_tag() and
 * set_picture_tag() methods of the given Encoder will be used to set the
 * metadata, with results going into the given Buffer. For FLAC, this
 * function does very little, with the actual processing handled by
 * metadata_callback(). This function will also read the actual PCM stream
 * parameters.
 */
int FlacDecoder::process_metadata(Encoder* encoder) {
    encoder_c = encoder;
    if (!process_until_end_of_metadata()) {
        mp3fs_debug("FLAC is invalid.");
        return -1;
    }

    if(encoder->set_stream_params(info.get_total_samples(),
                                  info.get_sample_rate(),
                                  info.get_channels()) == -1) {
        return -1;
    }

    return 0;
}

/*
 * Process a single frame of audio data. The encode_pcm_data() method
 * of the Encoder will be used to process the resulting audio data, with the
 * result going into the given Buffer. For FLAC, this function does little,
 * with most work handled by write_callback().
 */
int FlacDecoder::process_single_fr(Encoder* encoder, Buffer* buffer) {
    encoder_c = encoder;
    buffer_c = buffer;
    if (get_state() < FLAC__STREAM_DECODER_END_OF_STREAM) {
        if (!process_single()) {
            mp3fs_debug("Error reading FLAC.");
            return -1;
        }

        return 0;
    }

    return 1;
}

/*
 * Process metadata information from the FLAC file. This routine does all the
 * heavy lifting of handling FLAC metadata. It uses the set_text_tag() and
 * set_picture_tag() methods in the stored pointer to the Encoder to set
 * metadata in the output file.
 */
void FlacDecoder::metadata_callback(const FLAC__StreamMetadata* metadata) {
    switch (metadata->type) {
        case FLAC__METADATA_TYPE_STREAMINFO:
        {
            /* Set our copy of STREAMINFO data. */
            info = FLAC::Metadata::StreamInfo(metadata);

            mp3fs_debug("FLAC processing STREAMINFO");

            break;
        }
        case FLAC__METADATA_TYPE_VORBIS_COMMENT:
        {
            const FLAC::Metadata::VorbisComment vc(metadata);
            float filegainref = 89.0;
            float dbgain = INVALID_DB_GAIN;

            mp3fs_debug("FLAC processing VORBIS_COMMENT");

            for (unsigned int i=0; i<vc.get_num_comments(); ++i) {
                const FLAC::Metadata::VorbisComment::Entry comment = vc.get_comment(i);
                std::string fname(comment.get_field_name());
                /* Normalize tag name to uppercase. */
                std::transform(fname.begin(), fname.end(), fname.begin(), ::toupper);

                meta_map_t::const_iterator it = metatag_map.find(fname);
                if (it != metatag_map.end()) {
                    encoder_c->set_text_tag(it->second, comment.get_field_value());
                } else if (fname == "REPLAYGAIN_REFERENCE_LOUDNESS") {
                    filegainref = atof(comment.get_field_value());
                } else if (params.gainmode == 1
                           && fname == "REPLAYGAIN_ALBUM_GAIN") {
                    dbgain = atof(comment.get_field_value());
                } else if ((params.gainmode == 1 || params.gainmode == 2)
                           && dbgain == INVALID_DB_GAIN
                           && fname == "REPLAYGAIN_TRACK_GAIN") {
                    dbgain = atof(comment.get_field_value());
                }
            }

            /*
             * Use the Replay Gain tag to set volume scaling. The appropriate
             * value for dbgain is set in the above if statements according to
             * the value of gainmode. Obey the gainref option here.
             */
            if (dbgain != INVALID_DB_GAIN) {
                encoder_c->set_gain_db(params.gainref - filegainref + dbgain);
            }

            break;
        }
        case FLAC__METADATA_TYPE_PICTURE:
        {
            /* add a picture tag for each picture block */
            const FLAC::Metadata::Picture picture(metadata);

            mp3fs_debug("FLAC processing PICTURE");

            encoder_c->set_picture_tag(picture.get_mime_type(),
                                       picture.get_type(),
                                       (char*)picture.get_description(),
                                       picture.get_data(),
                                       picture.get_data_length());

            break;
        }
        default:
            break;
    }
}

/*
 * Process pcm audio data from the FLAC file. This function uses the
 * encode_pcm_data() methods in the stored pointer to the Encoder to encode
 * the data to the stored Buffer.
 */
FLAC__StreamDecoderWriteStatus
FlacDecoder::write_callback(const FLAC__Frame* frame,
                            const FLAC__int32* const buffer[]) {
    if(encoder_c->encode_pcm_data(buffer, frame->header.blocksize,
                                  frame->header.bits_per_sample,
                                  *buffer_c) == -1) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/* Error callback for FLAC */
void FlacDecoder::error_callback(FLAC__StreamDecoderErrorStatus status) {
    mp3fs_error("FLAC error: %s",
                FLAC__StreamDecoderErrorStatusString[status]);
}

/*
 * This function creates the metadata tag map from FLAC values to the standard
 * values in the enum in coders.h. It will be called only once to set the
 * metatag_map static variable.
 */
const FlacDecoder::meta_map_t FlacDecoder::create_meta_map() {
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

const FlacDecoder::meta_map_t FlacDecoder::metatag_map
    = FlacDecoder::create_meta_map();
