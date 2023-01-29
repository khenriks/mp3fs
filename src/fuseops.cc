/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC) to MP3 on the fly when opened and read. See README
 * for more details.
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2012 K. Henriksson
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

#define FUSE_USE_VERSION 26

#include <dirent.h>
#include <fcntl.h>
#include <fuse.h>
#include <fuse_common.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

#include "codecs/coders.h"
#include "logging.h"
#include "mp3fs.h"
#include "path.h"
#include "reader.h"
#include "transcode.h"

namespace {

constexpr int kBytesPerBlock = 512;

/**
 * Convert file extension from source to destination name.
 */
std::string convert_extension(const std::string& path) {
    const size_t ext_pos = path.rfind('.');

    if (ext_pos != std::string::npos &&
        Decoder::CreateDecoder(path.substr(ext_pos + 1)) != nullptr) {
        return path.substr(0, ext_pos + 1) + params.desttype;
    }

    return path;
}

int mp3fs_readlink(const char* p, char* buf, size_t size) {
    const Path path = Path::FromMp3fsRelative(p);
    Log(INFO) << "readlink " << path;

    const ssize_t len =
        readlink(path.transcode_source().c_str(), buf, size - 2);
    if (len == -1) {
        return -errno;
    }

    buf[len] = '\0';

    const size_t outlen = convert_extension(buf).copy(buf, size - 1);
    buf[outlen] = '\0';

    return 0;
}

int mp3fs_readdir(const char* p, void* buf, fuse_fill_dir_t filler,
                  off_t /*unused*/, struct fuse_file_info* /*unused*/) {
    const Path path = Path::FromMp3fsRelative(p);
    Log(INFO) << "readdir " << path;

    // Using a unique_ptr with a custom deleter ensures closedir gets called
    // before function exit.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
    const std::unique_ptr<DIR, decltype(&closedir)> dp(
        opendir(path.normal_source().c_str()), closedir);
#pragma GCC diagnostic pop
    if (!dp) {
        return -errno;
    }

    while (struct dirent* de = readdir(dp.get())) {
        const std::string origfile = path.normal_source() + "/" + de->d_name;

        struct stat st = {};
        if (lstat(origfile.c_str(), &st) == -1) {
            return -errno;
        }

        if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
            const size_t len = convert_extension(de->d_name)
                                   .copy(de->d_name, sizeof(de->d_name) - 1);
            de->d_name[len] = '\0';
        }

        if (filler(buf, de->d_name, &st, 0) != 0) {
            break;
        }
    }

    return 0;
}

int mp3fs_getattr(const char* p, struct stat* stbuf) {
    const Path path = Path::FromMp3fsRelative(p);
    Log(INFO) << "getattr " << path;

    /* pass-through for regular files */
    if (lstat(path.normal_source().c_str(), stbuf) == 0) {
        return 0;
    }

    if (lstat(path.transcode_source().c_str(), stbuf) == -1) {
        return -errno;
    }

    /*
     * Get size for resulting mp3 from regular file, otherwise it's a
     * symbolic link. */
    if (S_ISREG(stbuf->st_mode)) {
        Transcoder trans(path.transcode_source());
        if (!trans.open()) {
            return -errno;
        }

        stbuf->st_size = static_cast<off_t>(trans.get_size());
        stbuf->st_blocks =
            (stbuf->st_size + kBytesPerBlock - 1) / kBytesPerBlock;
    }

    return 0;
}

int mp3fs_open(const char* p, struct fuse_file_info* fi) {
    const Path path = Path::FromMp3fsRelative(p);
    Log(INFO) << "open " << path;

    const int fd = open(path.normal_source().c_str(), fi->flags);

    if (fd != -1) {  // File exists and was successfully opened.
        fi->fh = reinterpret_cast<uint64_t>(new FileReader(fd));
        return 0;
    }
    if (errno != ENOENT) {  // File exists but can't be opened.
        return -errno;
    }

    // File does not exist; try again after translating path.
    std::unique_ptr<Transcoder> trans(new Transcoder(path.transcode_source()));
    if (!trans->open()) {
        return -errno;
    }

    /* Store transcoder in the fuse_file_info structure. */
    fi->fh = reinterpret_cast<uint64_t>(trans.release());

    return 0;
}

int mp3fs_read(const char* path, char* buf, size_t size, off_t offset,
               struct fuse_file_info* fi) {
    Log(INFO) << "read " << path << ": " << size << " bytes from " << offset;

    auto* reader = reinterpret_cast<Reader*>(fi->fh);

    if (reader == nullptr) {
        Log(ERROR) << "Tried to read from unopen file: " << path;
        return -EBADF;
    }

    const ssize_t read = reader->read(buf, offset, size);

    if (read >= 0) {
        return static_cast<int>(read);
    }

    return -errno;
}

int mp3fs_statfs(const char* p, struct statvfs* stbuf) {
    const Path path = Path::FromMp3fsRelative(p);
    Log(INFO) << "statfs " << path;

    /* pass-through for regular files */
    if (statvfs(path.normal_source().c_str(), stbuf) == 0) {
        return 0;
    }

    if (statvfs(path.transcode_source().c_str(), stbuf) == 0) {
        return 0;
    }

    return -errno;
}

int mp3fs_release(const char* path, struct fuse_file_info* fi) {
    Log(INFO) << "release " << path;

    delete reinterpret_cast<Reader*>(fi->fh);

    return 0;
}

fuse_operations init_mp3fs_ops() {
    fuse_operations ops = {};

    ops.getattr = mp3fs_getattr;
    ops.readlink = mp3fs_readlink;
    ops.open = mp3fs_open;
    ops.read = mp3fs_read;
    ops.statfs = mp3fs_statfs;
    ops.release = mp3fs_release;
    ops.readdir = mp3fs_readdir;

    return ops;
}

}  // namespace

struct fuse_operations mp3fs_ops = init_mp3fs_ops();
