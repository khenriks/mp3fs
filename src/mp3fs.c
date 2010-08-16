/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC) to MP3 on the fly when opened and read. See README
 * for more details.
 *
 * Copyright (C) David Collett (daveco@users.sourceforge.net)
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
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "transcode.h"
#include "talloc.h"

struct mp3fs_params params = {
    .basepath           = NULL,
    .bitrate            = 0,
    .quality            = 5,
    .debug              = 0
};

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_KEEP_OPT
};

#define MP3FS_OPT(t, p, v) { t, offsetof(struct mp3fs_params, p), v }

static struct fuse_opt mp3fs_opts[] = {
    MP3FS_OPT("--quality=%d",     quality, 0),
    MP3FS_OPT("-d",               debug, 1),
    MP3FS_OPT("debug",            debug, 1),

    FUSE_OPT_KEY("-h",            KEY_HELP),
    FUSE_OPT_KEY("--help",        KEY_HELP),
    FUSE_OPT_KEY("-V",            KEY_VERSION),
    FUSE_OPT_KEY("--version",     KEY_VERSION),
    FUSE_OPT_KEY("-d",            KEY_KEEP_OPT),
    FUSE_OPT_KEY("debug",         KEY_KEEP_OPT),
    FUSE_OPT_END
};

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

    mp3fs_debug("%s: readlink", path);

    errno = 0;

    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }

    len = readlink(origpath, buf, size - 1);
    if (len == -1) {
        goto readlink_fail;
    }

    buf[len] = '\0';

readlink_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    char* origpath;
    DIR *dp;
    struct dirent *de;

    mp3fs_debug("%s: readdir", path);

    errno = 0;

    origpath = translate_path(path);
    if (!origpath) {
        goto translate_fail;
    }

    dp = opendir(origpath);
    if (!dp) {
        goto opendir_fail;
    }

    while ((de = readdir(dp))) {
        struct stat st;

        convert_path(de->d_name, 0);

        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
opendir_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_getattr(const char *path, struct stat *stbuf) {
    char* origpath;
    FileTranscoder f;

    mp3fs_debug("%s: getattr", path);

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

    f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL,
                  (char *)origpath);
    if (!f) {
        goto transcoder_fail;
    }

    stbuf->st_size = f->totalsize;
    stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;

    f->Finish(f);
    talloc_free(f);
transcoder_fail:
stat_fail:
passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
    char* origpath;
    int fd;
    FileTranscoder f;

    mp3fs_debug("%s: open", path);

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

    f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL,
                  (char *)origpath);
    if (!f) {
        goto transcoder_fail;
    }

    // store ourselves in the fuse_file_info structure
    fi->fh = (unsigned long) f;

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
    FileTranscoder f=NULL;
    int fd;
    int read = 0;

    mp3fs_debug("%s: reading %zu from %jd", path, size, offset);

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
    }

    /* File does exist, but can't be opened. */
    if (fd == -1 && errno != ENOENT) {
        goto open_fail;
    }

    // retrieve transcoder
    f = (FileTranscoder) fi->fh;

    if (!f) {
        mp3fs_debug("Tried to read from unopen file: %s", origpath);
        goto transcoder_fail;
    }
    read = f->Read(f, buf, offset, size);

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

    mp3fs_debug("%s: statfs", path);

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
    FileTranscoder f;

    mp3fs_debug("%s: release", path);

    f = (FileTranscoder) fi->fh;
    if (f) {
        f->Finish(f);
        talloc_free(f);
    }

    return 0;
}

/* We need synchronous reads. */
static void *mp3fs_init(struct fuse_conn_info *conn) {
    conn->async_read = 0;

    return NULL;
}


static struct fuse_operations mp3fs_ops = {
    .getattr  = mp3fs_getattr,
    .readlink = mp3fs_readlink,
    .readdir  = mp3fs_readdir,
    .open     = mp3fs_open,
    .read     = mp3fs_read,
    .statfs   = mp3fs_statfs,
    .release  = mp3fs_release,
    .init     = mp3fs_init,
};

void usage(char *name) {
    printf(""
        "Usage: %s flacdir,bitrate mountpoint [options]\n"
        "\n"
        "    Acceptable bitrates are 96, 112, 128, 160, 192, 224, 256, 320.\n"
        "    For a list of fuse options use -h after mountpoint.\n"
        "\n"
        "General options:\n"
        "    -o opt,[opt...]        mount options\n"
        "    -h   --help            print help\n"
        "    -V   --version         print version\n"
        "\n"
        "MP3FS options:\n"
        "    --quality=<0..9>       encoding quality:\n"
        "                           0=high/slow .. 9=poor/fast, 5=default\n"
        "\n", name);
}

static int mp3fs_opt_proc(void *data, const char *arg, int key,
                          struct fuse_args *outargs) {
    switch(key) {
        case FUSE_OPT_KEY_NONOPT:
            // check for flacdir and bitrate parameters
            if (!params.bitrate && !params.basepath) {
                char *rate;
                rate = strrchr(arg, ',');
                if (rate) {
                    rate[0] = '\0';
                    params.basepath = arg;
                    params.bitrate = atoi(rate + 1);
                    return 0;
                }
            }
            break;

        case KEY_HELP:
            usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-ho");
            fuse_main(outargs->argc, outargs->argv, &mp3fs_ops, NULL);
            exit(1);

        case KEY_VERSION:
            printf("MP3FS version %s\n", PACKAGE_VERSION);
            fuse_opt_add_arg(outargs, "--version");
            fuse_main(outargs->argc, outargs->argv, &mp3fs_ops, NULL);
            exit(0);
    }

    return 1;
}

int main(int argc, char *argv[]) {
    int ret;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &params, mp3fs_opts, mp3fs_opt_proc)) {
        fprintf(stderr, "Error parsing options.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (!params.bitrate || !params.basepath) {
        fprintf(stderr, "No valid flacdir or bitrate specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.basepath[0] != '/') {
        fprintf(stderr, "flacdir must be an absolute path.\n\n");
        usage(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(params.basepath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "flacdir is not a valid directory: %s\n\n",
                params.basepath);
        usage(argv[0]);
        return 1;
    }

    if (params.quality < 0 || params.quality > 9) {
        fprintf(stderr, "Invalid encoding quality value: %d\n\n",
                params.quality);
        usage(argv[0]);
        return 1;
    }

    /* Log to the screen if debug is enabled. */
    openlog("mp3fs", params.debug ? LOG_PERROR : 0, LOG_USER);

    mp3fs_debug("MP3FS options:\n"
                "basepath:  %s\n"
                "bitrate:   %d\n"
                "quality:   %d%s\n"
                "\n",
                params.basepath, params.bitrate,
                params.quality, params.quality == 5 ? " (default)" : "");

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
