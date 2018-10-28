/*
 * FLAC decoder class source for mp3fs
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

#include "codecs/flac_decoder.h"

#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logging.h"

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

    Log(DEBUG) << "FLAC ready to initialize.";

    int fd = open(filename, 0);
    if (fd < 0) {
        Log(ERROR) << "FLAC open failed.";
        return -1;
    }

    struct stat s;
    if (fstat(fd, &s) < 0) {
        Log(ERROR) << "FLAC stat failed.";
        close(fd);
        return -1;
    }
    mtime_ = s.st_mtime;

    FILE *file = fdopen(fd, "r");
    if (file == 0) {
        Log(ERROR) << "FLAC fdopen failed.";
        close(fd);
        return -1;
    }

    /* Initialise decoder */
    if (init(file) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        Log(ERROR) << "FLAC init failed.";
        fclose(file);
        return -1;
    }

    Log(DEBUG) << "FLAC initialized successfully.";

    return 0;
}

time_t FlacDecoder::mtime() {
    return mtime_;
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
    if (!process_until_end_of_metadata() || !has_streaminfo) {
        Log(ERROR) << "FLAC is invalid.";
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
int FlacDecoder::process_single_fr(Encoder* encoder) {
    encoder_c = encoder;
    if (get_state() < FLAC__STREAM_DECODER_END_OF_STREAM) {
        if (!process_single()) {
            Log(ERROR) << "Error reading FLAC.";
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
            has_streaminfo = true;

            Log(DEBUG) << "FLAC processing STREAMINFO";

            break;
        }
        case FLAC__METADATA_TYPE_VORBIS_COMMENT:
        {
            const FLAC::Metadata::VorbisComment vc(metadata);
            double gainref = Encoder::invalid_db,
                album_gain = Encoder::invalid_db,
                track_gain = Encoder::invalid_db;

            Log(DEBUG) << "FLAC processing VORBIS_COMMENT";

            for (unsigned int i=0; i<vc.get_num_comments(); ++i) {
                const FLAC::Metadata::VorbisComment::Entry comment = vc.get_comment(i);
                std::string fname(comment.get_field_name());
                /* Normalize tag name to uppercase. */
                std::transform(fname.begin(), fname.end(), fname.begin(), ::toupper);

                meta_map_t::const_iterator it = metatag_map.find(fname);
                if (it != metatag_map.end()) {
                    encoder_c->set_text_tag(it->second, comment.get_field_value());
                } else if (fname == "REPLAYGAIN_REFERENCE_LOUDNESS") {
                    gainref = atof(comment.get_field_value());
                } else if (fname == "REPLAYGAIN_ALBUM_GAIN") {
                    album_gain = atof(comment.get_field_value());
                } else if (fname == "REPLAYGAIN_TRACK_GAIN") {
                    track_gain = atof(comment.get_field_value());
                }
            }

            encoder_c->set_gain(gainref, album_gain, track_gain);

            break;
        }
        case FLAC__METADATA_TYPE_PICTURE:
        {
            /* add a picture tag for each picture block */
            const FLAC::Metadata::Picture picture(metadata);

            Log(DEBUG) << "FLAC processing PICTURE";

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
                                  frame->header.bits_per_sample) == -1) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

/* Error callback for FLAC */
void FlacDecoder::error_callback(FLAC__StreamDecoderErrorStatus status) {
    Log(ERROR) << "FLAC error: " <<
            FLAC__StreamDecoderErrorStatusString[status];
}

/*
 * This map associates FLAC values to the standard values in the enum in
 * coders.h.
 */
const FlacDecoder::meta_map_t FlacDecoder::metatag_map = {
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
