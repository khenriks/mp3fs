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
#include <sys/statvfs.h>

#include "transcode.h"
#include "talloc.h"

// determine the system's max path length
#ifdef PATH_MAX
    const int pathmax = PATH_MAX;
#else
    const int pathmax = 1024;
#endif

enum {
    KEY_HELP,
    KEY_VERSION,
};

static struct fuse_opt mp3fs_opts[] = {
    FUSE_OPT_KEY("-h",            KEY_HELP),
    FUSE_OPT_KEY("--help",        KEY_HELP),
    FUSE_OPT_KEY("-V",            KEY_VERSION),
    FUSE_OPT_KEY("--version",     KEY_VERSION),
    FUSE_OPT_END
};

static int mp3fs_readlink(const char *path, char *buf, size_t size) {
    int res;
    char name[pathmax];

    DEBUG(logfd, "%s: readlink\n", path);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    res = readlink(name, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi) {
    DIR *dp;
    struct dirent *de;
    char name[pathmax], *ptr;

    DEBUG(logfd, "%s: readdir\n", path);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    dp = opendir(name);
    if (dp == NULL)
        return -errno;

    while((de = readdir(dp)) != NULL) {
        struct stat st;

        strncpy(name, de->d_name, pathmax);
        ptr = name + strlen(name) - 1;
        while (ptr > name && *ptr != '.') --ptr;
        if (strcmp(ptr, ".flac") == 0) {
            strcpy(ptr, ".mp3");
        }

        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static int mp3fs_getattr(const char *path, struct stat *stbuf) {
    FileTranscoder f;
    char name[pathmax];
    int hold_errno;

    DEBUG(logfd, "%s: getattr\n", path);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    // pass-through for regular files
    if (lstat(name, stbuf) == 0)
        return 0;
    hold_errno = errno;

    f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
    if (f == NULL) {
        errno = hold_errno;
        return -errno;
    }

    if (lstat(f->orig_name, stbuf) == -1) {
        f->Finish(f);
        talloc_free(f);
        return -errno;
    }

    stbuf->st_size = f->totalsize;
    stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
    f->Finish(f);
    talloc_free(f);

    return 0;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
    int fd;
    FileTranscoder f;
    char name[pathmax];

    DEBUG(logfd, "%s: open\n", path);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    // If this is a real file, do nothing
    fd = open(name, fi->flags);

    // file does exist, but cant be opened, pass the error up
    if (fd == -1 && errno != ENOENT)
        return -errno;

    // file is real and can be opened, return success
    if (fd != -1) {
        close(fd);
        fi->fh = 0;
        return 0;
    }

    f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
    if (f==NULL)
        return -errno;

    // store ourselves in the fuse_file_info structure
    fi->fh = (unsigned long) f;

    return 0;
}

static int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    int fd, res;
    FileTranscoder f=NULL;
    char name[pathmax];

    DEBUG(logfd, "%s: reading %zu from %jd\n", path, size, offset);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    // If this is a real file, allow pass-through
    fd = open(name, O_RDONLY);
    if (fd != -1) {
        res = pread(fd, buf, size, offset);
        if (res == -1)
            res = -errno;

        close(fd);
        return res;
    }

    // file does exist, but cant be opened, pass the error up
    if (fd == -1 && errno != ENOENT)
        return -errno;

    // retrieve transcoder
    f = (FileTranscoder) fi->fh;

    if (f == NULL) {
        DEBUG(logfd, "Tried to read from unopen file: %s\n", name);
        return -errno;
    }
    return f->Read(f, buf, offset, size);
}

static int mp3fs_statfs(const char *path, struct statvfs *stbuf) {
    int res;
    char name[pathmax];

    DEBUG(logfd, "%s: statfs\n", path);

    strncpy(name, basepath, sizeof(name));
    strncat(name, path, sizeof(name) - strlen(name));

    res = statvfs(name, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
    FileTranscoder f;

    DEBUG(logfd, "%s: release\n", path);

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
    printf("\nUSAGE: %s flacdir,bitrate mountpoint [-o fuseopts]\n", name);
    printf("  acceptable bitrates are "
           "96, 112, 128, 160, 192, 224, 256, 320\n");
    printf("  for a list of fuse options use -h after mountpoint\n\n");
}

static int mp3fs_opt_proc(void *data, const char *arg, int key,
                          struct fuse_args *outargs) {
    switch(key) {
        case FUSE_OPT_KEY_NONOPT:
            // check for flacdir and bitrate parameters
            if (!bitrate && !basepath) {
                char *rate;
                rate = strrchr(arg, ',');
                if (rate) {
                    rate[0] = '\0';
                    basepath = arg;
                    bitrate = atoi(rate + 1);
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

    if (fuse_opt_parse(&args, NULL, mp3fs_opts, mp3fs_opt_proc)) {
        fprintf(stderr, "Error parsing options.\n");
        usage(argv[0]);
        return 1;
    }

    if (!bitrate || !basepath) {
        fprintf(stderr, "No valid bitrate or basepath specified.\n");
        usage(argv[0]);
        return 1;
    }

    // open the logfile
#ifdef __DEBUG__
    logfd = fopen("/tmp/mp3fs.log", "w");
#endif

    DEBUG(logfd, "MP3FS options:\n"
                 "basepath:  %s\n"
                 "bitrate:   %d\n"
                 "\n",
                 basepath, bitrate);

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

#ifdef __DEBUG__
    fclose(logfd);
#endif

    fuse_opt_free_args(&args);

    return ret;
}
