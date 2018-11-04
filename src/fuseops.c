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

/* Convert file name from source to destination name. The new extension will
 * be copied in place, and the passed path must be large enough to hold the
 * new name.
 */
void transcoded_name(char* path) {
    char* ext = strrchr(path, '.');

    if (ext && check_decoder(ext + 1)) {
        strcpy(ext + 1, params.desttype);
    }
}

/*
 * Given the destination (post-transcode) file name, determine the name of
 * the original file to be transcoded. The new extension will be copied in
 * place, and the passed path must be large enough to hold the new name.
 */
void find_original(char* path) {
    char* ext = strrchr(path, '.');

    if (ext && strcmp(ext + 1, params.desttype) == 0) {
        for (size_t i=0; i<decoder_list_len; ++i) {
            strcpy(ext + 1, decoder_list[i]);
            if (access(path, F_OK) == 0) {
                /* File exists with this extension */
                return;
            } else {
                /* File does not exist; not an error */
                errno = 0;
            }
        }
        /* Source file exists with no supported extension, restore path */
        strcpy(ext + 1, params.desttype);
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
    
    find_original(origpath);
    
    len = readlink(origpath, buf, size - 2);
    if (len == -1) {
        goto readlink_fail;
    }
    
    buf[len] = '\0';
    
    transcoded_name(buf);
    
readlink_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    (void)offset;
    (void)fi;
    char* origpath;
    char* origfile;
    DIR *dp;
    struct dirent *de;
    
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
    
    dp = opendir(origpath);
    if (!dp) {
        goto opendir_fail;
    }
    
    while ((de = readdir(dp))) {
        struct stat st;
        
        snprintf(origfile, strlen(origpath) + NAME_MAX + 2, "%s/%s", origpath,
                 de->d_name);
        
        if (lstat(origfile, &st) == -1) {
            goto stat_fail;
        } else {
            if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
                // TODO: Make this safe if converting from short to long ext.
                transcoded_name(de->d_name);
            }
        }
        
        if (filler(buf, de->d_name, &st, 0)) break;
    }
    
stat_fail:
    closedir(dp);
opendir_fail:
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
    
    find_original(origpath);
    
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
        
        stbuf->st_size = transcoder_get_size(trans);
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
        
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
    
    find_original(origpath);
    
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
    ssize_t read = 0;
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
    if (read >= 0) {
        return (int)read;
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

    /* pass-through for regular files */
    if (statvfs(origpath, stbuf) == 0) {
        goto passthrough;
    } else {
        /* Not really an error. */
        errno = 0;
    }

    find_original(origpath);

    statvfs(origpath, stbuf);

passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
    struct transcoder* trans;
    
    mp3fs_debug("release %s", path);
    
    trans = (struct transcoder*)fi->fh;
    if (trans) {
        transcoder_delete(trans);
    }
    
    return 0;
}

struct fuse_operations mp3fs_ops = {
    .getattr  = mp3fs_getattr,
    .readlink = mp3fs_readlink,
    .readdir  = mp3fs_readdir,
    .open     = mp3fs_open,
    .read     = mp3fs_read,
    .statfs   = mp3fs_statfs,
    .release  = mp3fs_release,
};
