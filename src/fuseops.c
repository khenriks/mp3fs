/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC) to MP3 on the fly when opened and read. See README
 * for more details.
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2012 Kristofer Henriksson
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include "transcode.h"

/*
 * Translate file names from FUSE to the original absolute path. A buffer
 * is allocated using malloc for the translated path. It is the caller's
 * responsibility to free it.
 */
char* translate_path(const char* path) {
    char* result;
    /*
     * Allocate buffer. The +2 is for the terminating '\0' and to
     * accomodate possibly translating .mp3 to .flac later.
     */
    result = malloc(strlen(params.basepath) + strlen(path) + 2);
    
    if (result) {
        strcpy(result, params.basepath);
        strcat(result, path);
    }
    
    return result;
}

/* Convert file extension between mp3 and flac. */
void convert_path(char* path, int toflac) {
    char* ptr;
    ptr = strrchr(path, '.');
    if (toflac) {
        if (ptr && strcmp(ptr, ".mp3") == 0) {
            strcpy(ptr, ".flac");
        }
    } else {
        if (ptr && strcmp(ptr, ".flac") == 0) {
            strcpy(ptr, ".mp3");
        }
    }
}

static int mp3fs_readlink(const char *path, char *buf, size_t size) {
    char* origpath;
    ssize_t len;
    
    mp3fs_debug("readlink %s", path);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    convert_path(origpath, 1);
    
    len = readlink(origpath, buf, size - 2);
    if (len == -1) {
        goto readlink_fail;
    }
    
    buf[len] = '\0';
    
    convert_path(buf, 0);
    
readlink_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    char* origpath;
    char* origfile;
    struct dirent **namelist;
    int n, i, skip = 0;
    
    mp3fs_debug("readdir %s", path);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    /* 2 for directory separator and NULL byte */
    origfile = malloc(strlen(origpath) + NAME_MAX + 2);
    if (!origfile) {
        goto origfile_fail;
    }
    
    n = scandir(origpath, &namelist, 0, alphasort);
    if (n < 0) {
        goto scandir_fail;
    }
    
    for (i = 0; i < n; i++) {
        if (!skip) {
            struct stat st;
            
            snprintf(origfile, strlen(origpath) + NAME_MAX + 2, "%s/%s", origpath,
                     namelist[i]->d_name);
            
            if (lstat(origfile, &st) == -1) {
                goto stat_fail;
            } else {
                if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
                    convert_path(namelist[i]->d_name, 0);
                }
            }
            
            skip = filler(buf, namelist[i]->d_name, &st, 0);
        }
        free(namelist[i]);
    }
    
stat_fail:
    free(namelist);
scandir_fail:
    free(origfile);
origfile_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_getattr(const char *path, struct stat *stbuf) {
    char* origpath;
    struct transcoder* trans;
    
    mp3fs_debug("getattr %s", path);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    /* pass-through for regular files */
    if (lstat(origpath, stbuf) == 0) {
        goto passthrough;
    } else {
        /* Not really an error. */
        errno = 0;
    }
    
    convert_path(origpath, 1);
    
    if (lstat(origpath, stbuf) == -1) {
        goto stat_fail;
    }
    
    /*
     * Get size for resulting mp3 from regular file, otherwise it's a
     * symbolic link. */
    if (S_ISREG(stbuf->st_mode)) {
        trans = transcoder_new(origpath);
        if (!trans) {
            goto transcoder_fail;
        }
        
        stbuf->st_size = trans->totalsize;
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
        
        transcoder_finish(trans);
        transcoder_delete(trans);
    }
    
transcoder_fail:
stat_fail:
passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
    char* origpath;
    struct transcoder* trans;
    int fd;
    
    mp3fs_debug("open %s", path);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    fd = open(origpath, fi->flags);
    
    /* File does exist, but can't be opened. */
    if (fd == -1 && errno != ENOENT) {
        goto open_fail;
    } else {
        /* Not really an error. */
        errno = 0;
    }
    
    /* File is real and can be opened. */
    if (fd != -1) {
        close(fd);
        goto passthrough;
    }
    
    convert_path(origpath, 1);
    
    trans = transcoder_new(origpath);
    if (!trans) {
        goto transcoder_fail;
    }
    
    /* Store transcoder in the fuse_file_info structure. */
    fi->fh = (uint64_t)trans;
    
transcoder_fail:
passthrough:
open_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    char* origpath;
    int fd;
    int read = 0;
    struct transcoder* trans;
    
    mp3fs_debug("read %s: %zu bytes from %jd", path, size, (intmax_t)offset);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    /* If this is a real file, pass the call through. */
    fd = open(origpath, O_RDONLY);
    if (fd != -1) {
        read = pread(fd, buf, size, offset);
        close(fd);
        goto passthrough;
    } else if (errno != ENOENT) {
        /* File does exist, but can't be opened. */
        goto open_fail;
    } else {
        /* File does not exist, and this is fine. */
        errno = 0;
    }
    
    trans = (struct transcoder*)fi->fh;
    
    if (!trans) {
        mp3fs_error("Tried to read from unopen file: %s", origpath);
        goto transcoder_fail;
    }
    
    read = transcoder_read(trans, buf, offset, size);
    
transcoder_fail:
passthrough:
open_fail:
    free(origpath);
translate_fail:
    if (read) {
        return read;
    } else {
        return -errno;
    }
}

static int mp3fs_statfs(const char *path, struct statvfs *stbuf) {
    char* origpath;
    
    mp3fs_debug("statfs %s", path);
    
    errno = 0;
    
    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }
    
    statvfs(origpath, stbuf);
    
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
    struct transcoder* trans;
    
    mp3fs_debug("release %s", path);
    
    trans = (struct transcoder*)fi->fh;
    if (trans) {
        transcoder_finish(trans);
        transcoder_delete(trans);
    }
    
    return 0;
}

/* We need synchronous reads. */
static void *mp3fs_init(struct fuse_conn_info *conn) {
    conn->async_read = 0;
    
    return NULL;
}

struct fuse_operations mp3fs_ops = {
    .getattr  = mp3fs_getattr,
    .readlink = mp3fs_readlink,
    .readdir  = mp3fs_readdir,
    .open     = mp3fs_open,
    .read     = mp3fs_read,
    .statfs   = mp3fs_statfs,
    .release  = mp3fs_release,
    .init     = mp3fs_init,
};
