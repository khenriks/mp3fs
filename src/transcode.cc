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

#include "buffer.h"
#include "logging.h"
#include "stats_cache.h"
#include "ffmpeg_transcoder.h"

/* Transcoder parameters for open mp3 */
struct transcoder {
    Buffer buffer;
    std::string filename;
    size_t encoded_filesize;
    bool finished;

    FfmpegTranscoder transcoder;
};

namespace {

StatsCache stats_cache;

/*
 * Transcode the buffer until the buffer has enough or until an error occurs.
 * The buffer needs at least 'end' bytes before transcoding stops. Returns true
 * if no errors and false otherwise.
 */
bool transcode_until(struct transcoder* trans, size_t end) {
    bool success = true;

    while (!trans->finished && trans->buffer.tell() < end) {
        int stat = trans->transcoder.process_single_fr();

        if (stat == -1 || (stat == 1 && transcoder_finish(trans) == -1)) {
            errno = EIO;
            success = false;
            break;
        }
    }
    return success;
}

}

/* Use "C" linkage to allow access from C code. */
extern "C" {

int transcoder_cached_filesize(const char* filename, struct stat *stbuf) {
    mp3fs_debug("Retrieving encoded size for %s.", filename);

    size_t encoded_filesize;
    if (stats_cache.get_filesize(filename, stbuf->st_mtime, encoded_filesize)) {
        stbuf->st_size = encoded_filesize;
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
        return true;
    }
    else {
        return false;
    }
}

/* Allocate and initialize the transcoder */

struct transcoder* transcoder_new(const char* filename, int open_out) {
    mp3fs_debug("Creating transcoder object for %s.", filename);

    /* Allocate transcoder structure */
    struct transcoder* trans = new struct transcoder;
    if (!trans) {
        goto trans_fail;
    }

    /* Create Encoder and Decoder objects. */
    trans->filename = filename;
    trans->encoded_filesize = 0;
    trans->finished = false;

    mp3fs_debug("Ready to initialise decoder.");

    if (trans->transcoder.open_file(filename) < 0) {
        goto init_fail;
    }

    mp3fs_debug("Transcoder initialised successfully.");

    stats_cache.get_filesize(trans->filename, trans->transcoder.mtime(), trans->encoded_filesize);

    if (open_out)
    {
        if (trans->transcoder.open_out_file(&trans->buffer, params.desttype) == -1) {
            goto init_fail;
        }

        mp3fs_debug("Output file opened.");
    }

    return trans;

init_fail:
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

#ifndef HAVE_FFMPEG
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
#endif

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
            (transcoder_get_size(trans) - id3v1_tag_length)) {

        memcpy(buff, trans->transcoder.id3v1tag(), id3v1_tag_length);

        return len;
    }

    bool success = true;
    success = transcode_until(trans, offset + len);

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
    // decoder cleanup
    time_t decoded_file_mtime = 0;

    fprintf(stderr, "FINISH FILE\n");

    decoded_file_mtime = trans->transcoder.mtime();

    // encoder cleanup
    int len = trans->transcoder.encode_finish(trans->buffer);
    if (len == -1) {
        return -1;
    }

    /* Check encoded buffer size. */
    trans->encoded_filesize = trans->transcoder.get_actual_size();
    trans->finished = true;

    mp3fs_debug("Finishing file. Predicted size: %zu, final size: %zu.",
                trans->transcoder.calculate_size(), trans->encoded_filesize);

    if (params.statcachesize > 0 && trans->encoded_filesize != 0) {
        stats_cache.put_filesize(trans->filename, trans->encoded_filesize,
                                 decoded_file_mtime);
    }

    return 0;
}

/* Free the transcoder structure. */

void transcoder_delete(struct transcoder* trans) {
    fprintf(stderr, "CLOSE FILES/DELETE TRANSCODER\n");
    delete trans;
}

/* Return size of output file, as computed by Encoder. */
size_t transcoder_get_size(struct transcoder* trans) {
    if (trans->encoded_filesize != 0) {
        return trans->encoded_filesize;
    } else
        return trans->transcoder.calculate_size();
}
}

size_t transcoder_actual_size(struct transcoder* trans) {
    return trans->buffer.actual_size();
}

size_t transcoder_tell(struct transcoder* trans) {
    return trans->buffer.tell();
}

void mp3fs_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(DEBUG, format, args);
    va_end(args);
}

void mp3fs_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_with_level(WARNING, format, args);
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
    static const std::map<std::string, Logging::level> level_map = {
        {"DEBUG", DEBUG},
        {"WARNING", WARNING},
        {"INFO", INFO},
        {"ERROR", ERROR},
    };

    auto it = level_map.find(max_level);

    if (it == level_map.end()) {
        fprintf(stderr, "Invalid logging level string: %s\n", max_level);
        return false;
    }

    return InitLogging(logfile, it->second, to_stderr, to_syslog);
}
