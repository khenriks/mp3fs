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
#include <errno.h>
#include <fuse.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string>

#include "codecs/coders.h"
#include "logging.h"
#include "mp3fs.h"
#include "path.h"
#include "reader.h"
#include "transcode.h"

namespace {

/**
 * Convert file extension from source to destination name.
 */
void convert_extension(char* path) {
    char* ext = strrchr(path, '.');

    if (ext && check_decoder(ext + 1)) {
        strcpy(ext + 1, params.desttype);
    }
}

int mp3fs_readlink(const char *p, char *buf, size_t size) {
    Path path = Path::FromMp3fsRelative(p);
    Log(DEBUG) << "readlink " << path;

    errno = 0;

    ssize_t len = readlink(path.TranscodeSource().c_str(), buf, size - 2);
    if (len == -1) {
        return -errno;
    }

    buf[len] = '\0';

    convert_extension(buf);

    return 0;
}

int mp3fs_readdir(const char *p, void *buf, fuse_fill_dir_t filler,
                  off_t, struct fuse_file_info*) {
    Path path = Path::FromMp3fsRelative(p);
    Log(DEBUG) << "readdir " << path;

    errno = 0;

    // Using a unique_ptr with a custom deleter ensures closedir gets called
    // before function exit.
    std::unique_ptr<DIR, int(*)(DIR*)> dp(opendir(path.NormalSource().c_str()),
                                          closedir);
    if (!dp) {
        return -errno;
    }

    while (struct dirent* de = readdir(dp.get())) {
        std::string origfile = path.NormalSource() + "/" + de->d_name;

        struct stat st;
        if (lstat(origfile.c_str(), &st) == -1) {
            return -errno;
        } else {
            if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
                // TODO: Make this safe if converting from short to long ext.
                convert_extension(de->d_name);
            }
        }

        if (filler(buf, de->d_name, &st, 0)) break;
    }

    return 0;
}

int mp3fs_getattr(const char *p, struct stat *stbuf) {
    Path path = Path::FromMp3fsRelative(p);
    Log(DEBUG) << "getattr " << path;

    errno = 0;

    /* pass-through for regular files */
    if (lstat(path.NormalSource().c_str(), stbuf) == 0) {
        return 0;
    } else {
        /* Not really an error. */
        errno = 0;
    }

    if (lstat(path.TranscodeSource().c_str(), stbuf) == -1) {
        return -errno;
    }

    /*
     * Get size for resulting mp3 from regular file, otherwise it's a
     * symbolic link. */
    if (S_ISREG(stbuf->st_mode)) {
        Transcoder trans(path.TranscodeSource());
        if (!trans.open()) {
            return -errno;
        }

        stbuf->st_size = trans.get_size();
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
    }

    return 0;
}

int mp3fs_open(const char *p, struct fuse_file_info *fi) {
    Path path = Path::FromMp3fsRelative(p);
    Log(DEBUG) << "open " << path;

    errno = 0;

    int fd = open(path.NormalSource().c_str(), fi->flags);

    if (fd != -1) {  // File exists and was successfully opened.
        fi->fh = reinterpret_cast<uint64_t>(new FileReader(fd));
        return 0;
    } else if (errno != ENOENT) {  // File exists but can't be opened.
        return -errno;
    }

    // File does not exist; try again after translating path.
    errno = 0;

    std::unique_ptr<Transcoder> trans(new Transcoder(path.TranscodeSource()));
    if (!trans->open()) {
        return -errno;
    }

    /* Store transcoder in the fuse_file_info structure. */
    fi->fh = reinterpret_cast<uint64_t>(trans.release());

    return 0;
}

int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {
    Log(DEBUG) << "read " << path << ": " << size << " bytes from " << offset;

    errno = 0;

    Reader* reader = reinterpret_cast<Reader*>(fi->fh);

    if (!reader) {
        Log(ERROR) << "Tried to read from unopen file: " << path;
        return 0;
    }

    ssize_t read = reader->read(buf, offset, size);

    if (read >= 0) {
        return (int)read;
    } else {
        return -errno;
    }
}

int mp3fs_statfs(const char *p, struct statvfs *stbuf) {
    Path path = Path::FromMp3fsRelative(p);
    Log(DEBUG) << "statfs " << path;

    errno = 0;

    /* pass-through for regular files */
    if (statvfs(path.NormalSource().c_str(), stbuf) == 0) {
        return 0;
    } else {
        /* Not really an error. */
        errno = 0;
    }

    statvfs(path.TranscodeSource().c_str(), stbuf);

    return -errno;
}

int mp3fs_release(const char *path, struct fuse_file_info *fi) {
    Log(DEBUG) << "release " << path;

    Reader* reader = reinterpret_cast<Reader*>(fi->fh);
    if (reader) {
        delete reader;
    }

    return 0;
}

fuse_operations init_mp3fs_ops() {
    fuse_operations ops;

    ops.getattr  = mp3fs_getattr;
    ops.readlink = mp3fs_readlink;
    ops.open     = mp3fs_open;
    ops.read     = mp3fs_read;
    ops.statfs   = mp3fs_statfs;
    ops.release  = mp3fs_release;
    ops.readdir  = mp3fs_readdir;

    return ops;
}

}

struct fuse_operations mp3fs_ops = init_mp3fs_ops();
