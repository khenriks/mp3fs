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

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <ctime>  // IWYU pragma: keep (time_t)
#include <limits>
#include <mutex>

#include "codecs/coders.h"
#include "logging.h"
#include "mp3fs.h"
#include "stats_cache.h"

namespace {

StatsCache stats_cache;

}

bool Transcoder::open() {
    /* Create Encoder and Decoder objects. */
    size_t dot_idx = filename_.rfind('.');
    if (dot_idx != std::string::npos) {
        decoder_ = Decoder::CreateDecoder(filename_.substr(dot_idx + 1));
    }
    if (!decoder_) {
        errno = EIO;
        return false;
    }

    Log(DEBUG) << "Ready to initialize decoder.";

    if (decoder_->open_file(filename_.c_str()) == -1) {
        errno = EIO;
        return false;
    }

    Log(DEBUG) << "Decoder initialized successfully.";

    stats_cache.get_filesize(filename_, decoder_->mtime(), &encoded_filesize_);
    encoder_ = Encoder::CreateEncoder(params.desttype, &buffer_);
    if (!encoder_) {
        errno = EIO;
        return false;
    }

    /*
     * Process metadata. The Decoder will call the Encoder to set appropriate
     * tag values for the output file.
     */
    if (decoder_->process_metadata(encoder_.get()) == -1) {
        Log(ERROR) << "Error processing metadata.";
        errno = EIO;
        return false;
    }

    Log(DEBUG) << "Metadata processing finished.";

    /* Render tag from Encoder to Buffer. */
    if (encoder_->render_tag(encoded_filesize_) == -1) {
        Log(ERROR) << "Error rendering tag in Encoder.";
        errno = EIO;
        return false;
    }

    Log(DEBUG) << "Tag written to Buffer.";

    return true;
}

ssize_t Transcoder::read(char* buff, off_t offset, size_t len) {
    std::lock_guard<std::mutex> l(mutex_);
    Log(DEBUG) << "Reading " << len << " bytes from offset " << offset << ".";
    if (static_cast<size_t>(offset) > get_size()) {
        return 0;
    }
    if (offset + len > get_size()) {
        len = get_size() - offset;
        Log(DEBUG) << "Actual length to read: " << len;
    }

    // If the requested data has already been filled into the buffer, simply
    // copy it out.
    if (buffer_.valid_bytes(offset, len)) {
        buffer_.copy_into(reinterpret_cast<uint8_t*>(buff), offset, len);
        return len;
    }

    while (decoder_ && encoder_ &&
           buffer_.tell() < (encoder_->no_partial_encode()
                                 ? std::numeric_limits<size_t>::max()
                                 : offset + len)) {
        int stat = decoder_->process_single_fr(encoder_.get());
        if (stat == -1 || (stat == 1 && !finish())) {
            errno = EIO;
            return -1;
        }
    }

    // truncate if we didn't actually get len
    if (buffer_.tell() < offset + len) {
        len = std::max<off_t>(buffer_.tell() - offset, 0);
    }

    buffer_.copy_into(reinterpret_cast<uint8_t*>(buff), offset, len);

    Log(DEBUG) << "Successfully read " << len << " bytes.";
    return len;
}

size_t Transcoder::get_size() const {
    if (encoded_filesize_ != 0) {
        return encoded_filesize_;
    }
    if (encoder_) {
        return encoder_->calculate_size();
    }

    return buffer_.tell();
}

bool Transcoder::finish() {
    // Decoder cleanup
    time_t decoded_file_mtime = 0;
    if (decoder_) {
        decoded_file_mtime = decoder_->mtime();
        decoder_.reset(nullptr);
    }

    // Encoder cleanup
    if (encoder_) {
        if (encoder_->encode_finish() == -1) {
            return false;
        }

        /* Check encoded buffer size. */
        encoded_filesize_ = encoder_->get_actual_size();
        Log(DEBUG) << "Finishing file. Predicted size: "
                   << encoder_->calculate_size()
                   << ", final size: " << encoded_filesize_;
        encoder_.reset(nullptr);
    }

    if (params.statcachesize > 0 && encoded_filesize_ != 0) {
        stats_cache.put_filesize(filename_, encoded_filesize_,
                                 decoded_file_mtime);
    }

    return true;
}
