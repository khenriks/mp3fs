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

#ifndef MP3FS_TRANSCODE_H_
#define MP3FS_TRANSCODE_H_

#include <sys/types.h>

#include <memory>
#include <mutex>
#include <ostream>
#include <string>

#include "buffer.h"
#include "codecs/coders.h"
#include "logging.h"
#include "reader.h"

/* Transcoder for open file */
class Transcoder : public Reader {
 public:
    explicit Transcoder(const std::string& filename) : filename_(filename) {
        Log(DEBUG) << "Creating transcoder object for " << filename;
    }

    ~Transcoder() override = default;

    /** Initialize the transcoder. This is equivalent of a file open. */
    bool open();

    /** Read bytes into the internal buffer and into the given buffer. */
    ssize_t read(char* buff, off_t offset, size_t len) override;

    /** Return size of output file, as computed by Encoder. */
    size_t get_size() const { return buffer_.size(); }

 private:
    /** Close the input file and free everything but the buffer. */
    bool finish();

    Buffer buffer_;
    std::string filename_;

    std::unique_ptr<Encoder> encoder_;
    std::unique_ptr<Decoder> decoder_;

    std::mutex mutex_;
};

#endif  // MP3FS_TRANSCODE_H_
