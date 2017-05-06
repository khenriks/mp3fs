/*
 * FileTranscoder interface for MP3FS
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
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

#include "transcode.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <vector>

#include "coders.h"
#include "logging.h"
#include "mp3_encoder.h"
#include "stats_cache.h"

/* Transcoder parameters for open mp3 */
struct transcoder {
    Buffer buffer;
    std::string filename;
    size_t encoded_filesize;

    Encoder* encoder;
    Decoder* decoder;
};

namespace {

StatsCache stats_cache;

/*
 * Transcode the buffer until the buffer has enough or until an error occurs.
 * The buffer needs at least 'end' bytes before transcoding stops. Returns true
 * if no errors and false otherwise.
 */
bool transcode_until(struct transcoder* trans, size_t end) {
    while (trans->encoder && trans->buffer.tell() < end) {
        int stat = trans->decoder->process_single_fr(trans->encoder,
                                                     &trans->buffer);
        if (stat == -1 || (stat == 1 && transcoder_finish(trans) == -1)) {
            errno = EIO;
            return false;
        }
    }
    return true;
}

}

/* Use "C" linkage to allow access from C code. */
extern "C" {

/* Allocate and initialize the transcoder */

struct transcoder* transcoder_new(char* filename) {
    mp3fs_debug("Creating transcoder object for %s", filename);

    /* Allocate transcoder structure */
    struct transcoder* trans = new struct transcoder;
    if (!trans) {
        goto trans_fail;
    }

    /* Create Encoder and Decoder objects. */
    trans->filename = filename;
    trans->encoded_filesize = 0;
    trans->decoder = Decoder::CreateDecoder(strrchr(filename, '.') + 1);
    if (!trans->decoder) {
        goto decoder_fail;
    }

    mp3fs_debug("Ready to initialize decoder.");

    if (trans->decoder->open_file(filename) == -1) {
        goto init_fail;
    }

    mp3fs_debug("Decoder initialized successfully.");

    stats_cache.get_filesize(trans->filename, trans->decoder->mtime(),
            trans->encoded_filesize);
    trans->encoder = Encoder::CreateEncoder(params.desttype,
            trans->encoded_filesize);
    if (!trans->encoder) {
        goto encoder_fail;
    }

    /*
     * Process metadata. The Decoder will call the Encoder to set appropriate
     * tag values for the output file.
     */
    if (trans->decoder->process_metadata(trans->encoder) == -1) {
        mp3fs_debug("Error processing metadata.");
        goto init_fail;
    }

    mp3fs_debug("Metadata processing finished.");

    /* Render tag from Encoder to Buffer. */
    if (trans->encoder->render_tag(trans->buffer) == -1) {
        mp3fs_debug("Error rendering tag in Encoder.");
        goto init_fail;
    }

    mp3fs_debug("Tag written to Buffer.");

    return trans;

encoder_fail:
    delete trans->decoder;
decoder_fail:
init_fail:
    delete trans->encoder;

    delete trans;

trans_fail:
    return NULL;
}

/* Read some bytes into the internal buffer and into the given buffer. */

ssize_t transcoder_read(struct transcoder* trans, char* buff, off_t offset,
                        size_t len) {
    mp3fs_debug("Reading %zu bytes from offset %jd.", len, (intmax_t)offset);
    if ((size_t)offset > transcoder_get_size(trans)) {
        return -1;
    }
    if (offset + len > transcoder_get_size(trans)) {
        len = transcoder_get_size(trans) - offset;
    }

    // TODO: Avoid favoring MP3 in program structure.
    /*
     * If we are encoding to MP3 and the requested data overlaps the ID3v1 tag
     * at the end of the file, do not encode data first up to that position.
     * This optimizes the case where applications read the end of the file
     * first to read the ID3v1 tag.
     */
    if (strcmp(params.desttype, "mp3") == 0 &&
        (size_t)offset > trans->buffer.tell()
        && offset + len >
        (transcoder_get_size(trans) - Mp3Encoder::id3v1_tag_length)) {
        trans->buffer.copy_into((uint8_t*)buff, offset, len);

        return len;
    }

    bool success = true;
    if (trans->decoder && trans->encoder) {
        if (strcmp(params.desttype, "mp3") == 0 && params.vbr) {
            /*
             * The Xing data (which is pretty close to the beginning of the
             * file) cannot be determined until the entire file is encoded, so
             * transcode the entire file for any read.
             */
            success = transcode_until(trans,
                    std::numeric_limits<size_t>::max());
        } else {
            success = transcode_until(trans, offset + len);
        }
    }
    if (!success) {
        return -1;
    }

    // truncate if we didn't actually get len
    if (trans->buffer.tell() < (size_t) offset) {
        len = 0;
    } else if (trans->buffer.tell() < offset + len) {
        len = trans->buffer.tell() - offset;
    }

    trans->buffer.copy_into((uint8_t*)buff, offset, len);

    return len;
}

/* Close the input file and free everything but the initial buffer. */

int transcoder_finish(struct transcoder* trans) {
    // flac cleanup
    time_t decoded_file_mtime = 0;
    if (trans->decoder) {
        decoded_file_mtime = trans->decoder->mtime();
        delete trans->decoder;
        trans->decoder = NULL;
    }

    // lame cleanup
    if (trans->encoder) {
        int len = trans->encoder->encode_finish(trans->buffer);
        if (len == -1) {
            return -1;
        }

        /* Check encoded buffer size. */
        trans->encoded_filesize = trans->encoder->get_actual_size();
        mp3fs_debug("Finishing file. Predicted size: %zu, final size: %zu",
                    trans->encoder->calculate_size(), trans->encoded_filesize);
        delete trans->encoder;
        trans->encoder = NULL;
    }

    if (params.statcachesize > 0 && trans->encoded_filesize != 0) {
        stats_cache.put_filesize(trans->filename, trans->encoded_filesize,
                decoded_file_mtime);
    }

    return 0;
}

/* Free the transcoder structure. */

void transcoder_delete(struct transcoder* trans) {
    if (trans->decoder) {
        delete trans->decoder;
    }
    if (trans->encoder) {
        delete trans->encoder;
    }
    delete trans;
}

/* Return size of output file, as computed by Encoder. */
size_t transcoder_get_size(struct transcoder* trans) {
    if (trans->encoded_filesize != 0) {
        return trans->encoded_filesize;
    } else if (trans->encoder) {
        return trans->encoder->calculate_size();
    } else {
        return trans->buffer.tell();
    }
}

void mp3fs_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(DEBUG, format, args);
    va_end(args);
}

void mp3fs_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(ERROR, format, args);
    va_end(args);
}

int init_logging(const char* logfile, const char* max_level, int to_stderr,
                 int to_syslog) {
    std::string max_level_str = max_level;
    Logging::level lev;
    if (max_level_str == "DEBUG") {
        lev = DEBUG;
    } else if (max_level_str == "INFO") {
        lev = INFO;
    } else if (max_level_str == "ERROR") {
        lev = ERROR;
    } else {
        fprintf(stderr, "Invalid logging level string: %s\n", max_level);
        return false;
    }
    return InitLogging(logfile, lev, to_stderr, to_syslog);
}

}
