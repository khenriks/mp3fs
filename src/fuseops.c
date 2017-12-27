/*
 * FFMPEGFS: A read-only FUSE filesystem which transcodes audio formats
 * to MP3/MP4 on the fly when opened and read. See README
 * for more details.
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2012 K. Henriksson
 * Copyright (C) 2017 FFmpeg support by Norbert Schlia (nschlia@oblivion-software.de)
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
#include "ffmpeg_utils.h"
#include "coders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#define CLOCKID CLOCK_REALTIME
#define SIG SIGRTMIN

static sigset_t mask;
static timer_t timerid;

static void handler(int sig, __attribute__((unused)) siginfo_t *si, __attribute__((unused)) void *uc);
static int start_maintenance_timer(time_t interval);

/*
 * Translate file names from FUSE to the original absolute path. A buffer
 * is allocated using malloc for the translated path. It is the caller's
 * responsibility to free it.
 */
char* translate_path(const char* path)
{
    char* result;
    /*
     * Allocate buffer. The +2 is for the terminating '\0' and to
     * accomodate possibly translating .mp3 to .flac later.
     */
    result = malloc(strlen(params.m_basepath) + strlen(path) + 2);

    if (result)
    {
        strcpy(result, params.m_basepath);
        strcat(result, path);
    }

    return result;
}

/* Convert file name from source to destination name. The new extension will
 * be copied in place, and the passed path must be large enough to hold the
 * new name.
 */
void transcoded_name(char* path)
{
    char* ext = strrchr(path, '.');

    if (ext && check_decoder(ext + 1))
    {
        strcpy(ext + 1, params.m_desttype);
    }
}

/*
 * Given the destination (post-transcode) file name, determine the name of
 * the original file to be transcoded. The new extension will be copied in
 * place, and the passed path must be large enough to hold the new name.
 */
void find_original(char* path)
{
    char* ext = strrchr(path, '.');

    if (ext && strcmp(ext + 1, params.m_desttype) == 0)
    {
        for (size_t i=0; decoder_list[i]; ++i)
        {
            strcpy(ext + 1, decoder_list[i]);
            if (access(path, F_OK) == 0)
            {
                /* File exists with this extension */
                return;
            }
            else
            {
                /* File does not exist; not an error */
                errno = 0;
            }
        }
        /* Source file exists with no supported extension, restore path */
        strcpy(ext + 1, params.m_desttype);
    }
}

static int ffmpegfs_readlink(const char *path, char *buf, size_t size)
{
    char* origpath;
    ssize_t len;

    ffmpegfs_trace("readlink %s", path);

    //errno = 0;

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    find_original(origpath);

    len = readlink(origpath, buf, size - 2);
    if (len == -1)
    {
        goto readlink_fail;
    }

    buf[len] = '\0';

    transcoded_name(buf);

    errno = 0;  // Just to make sure - reset any error

readlink_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int ffmpegfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
    (void)offset;
    (void)fi;
    char* origpath;
    char* origfile;
    DIR *dp;
    struct dirent *de;

    ffmpegfs_trace("readdir %s", path);

    //errno = 0;

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    /* 2 for directory separator and NULL byte */
    origfile = malloc(strlen(origpath) + NAME_MAX + 2);
    if (!origfile)
    {
        goto origfile_fail;
    }

    dp = opendir(origpath);
    if (!dp)
    {
        goto opendir_fail;
    }

    while ((de = readdir(dp)))
    {
        struct stat st;

        snprintf(origfile, strlen(origpath) + NAME_MAX + 2, "%s/%s", origpath, de->d_name);

        if (lstat(origfile, &st) == -1)
        {
            goto stat_fail;
        }
        else
        {
            if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))
            {
                // TODO: Make this safe if converting from short to long ext.
                transcoded_name(de->d_name);
            }
        }

        if (filler(buf, de->d_name, &st, 0)) break;
    }

    errno = 0;  // Just to make sure - reset any error

stat_fail:
    closedir(dp);
opendir_fail:
    free(origfile);
origfile_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int ffmpegfs_getattr(const char *path, struct stat *stbuf)
{
    char* origpath;

    ffmpegfs_trace("getattr %s", path);

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    /* pass-through for regular files */
    if (lstat(origpath, stbuf) == 0)
    {
        errno = 0;
        goto passthrough;
    }
    else
    {
        /* Not really an error. */
        errno = 0;
    }

    find_original(origpath);

    if (lstat(origpath, stbuf) == -1)
    {
        goto stat_fail;
    }

    /*
     * Get size for resulting output file from regular file, otherwise it's a
     * symbolic link. */
    if (S_ISREG(stbuf->st_mode))
    {

        if (!transcoder_cached_filesize(origpath, stbuf))
        {
            struct Cache_Entry* cache_entry;

            cache_entry = transcoder_new(origpath, 0);
            if (!cache_entry)
            {
                goto transcoder_fail;
            }

#if defined __x86_64__ || !defined __USE_FILE_OFFSET64
            stbuf->st_size = (__off_t)transcoder_get_size(cache_entry);
#else
            stbuf->st_size = (__off64_t)transcoder_get_size(cache_entry);
#endif
            stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;

            transcoder_delete(cache_entry);
        }
    }

    errno = 0;  // Just to make sure - reset any error

transcoder_fail:
stat_fail:
passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

int ffmpegfs_fgetattr(const char *filename, struct stat * stbuf, struct fuse_file_info *fi)
{
    char* origpath;

    ffmpegfs_trace("fgetattr %s", filename);

    errno = 0;

    origpath = translate_path(filename);
    if (!origpath)
    {
        goto translate_fail;
    }

    /* pass-through for regular files */
    if (lstat(origpath, stbuf) == 0)
    {
        goto passthrough;
    }
    else
    {
        /* Not really an error. */
        errno = 0;
    }

    find_original(origpath);

    if (lstat(origpath, stbuf) == -1)
    {
        goto stat_fail;
    }

    /*
     * Get size for resulting output file from regular file, otherwise it's a
     * symbolic link. */
    if (S_ISREG(stbuf->st_mode))
    {

        struct Cache_Entry* cache_entry;

        cache_entry = (struct Cache_Entry*)fi->fh;

        if (!cache_entry)
        {
            ffmpegfs_error("Tried to stat unopen file: %s.", filename);
            errno = EBADF;
            goto transcoder_fail;
        }

#if defined __x86_64__ || !defined __USE_FILE_OFFSET64
        stbuf->st_size = (__off_t)transcoder_buffer_tell(cache_entry);
#else
        stbuf->st_size = (__off64_t)transcoder_buffer_tell(cache_entry);
#endif
        stbuf->st_blocks = (stbuf->st_size + 512 - 1) / 512;
    }

    errno = 0;  // Just to make sure - reset any error

transcoder_fail:
stat_fail:
passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

static int ffmpegfs_open(const char *path, struct fuse_file_info *fi)
{
    char* origpath;
    struct Cache_Entry* cache_entry;
    int fd;

    ffmpegfs_trace("open %s", path);

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    fd = open(origpath, fi->flags);

    /* File does exist, but can't be opened. */
    if (fd == -1 && errno != ENOENT)
    {
        goto open_fail;
    }
    else
    {
        /* Not really an error. */
        errno = 0;
    }

    /* File is real and can be opened. */
    if (fd != -1)
    {
        close(fd);
        goto passthrough;
    }

    find_original(origpath);

    cache_entry = transcoder_new(origpath, 1);
    if (!cache_entry)
    {
        goto transcoder_fail;
    }

    /* Store transcoder in the fuse_file_info structure. */
    fi->fh = (uint64_t)cache_entry;

    // Clear errors
    errno = 0;

transcoder_fail:
passthrough:
open_fail:
    free(origpath);
translate_fail:
    return -errno;
}

static int ffmpegfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    char* origpath;
    int fd;
    ssize_t read = 0;
    struct Cache_Entry* cache_entry;

    ffmpegfs_trace("read %s: %zu bytes from %jd.", path, size, (intmax_t)offset);

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    /* If this is a real file, pass the call through. */
    fd = open(origpath, O_RDONLY);
    if (fd != -1)
    {
        read = pread(fd, buf, size, offset);
        close(fd);
        goto passthrough;
    }
    else if (errno != ENOENT)
    {
        /* File does exist, but can't be opened. */
        goto open_fail;
    }
    else
    {
        /* File does not exist, and this is fine. */
        errno = 0;
    }

    cache_entry = (struct Cache_Entry*)fi->fh;

    if (!cache_entry)
    {
        ffmpegfs_error("Tried to read from unopen file: %s.", origpath);
        goto transcoder_fail;
    }

    read = transcoder_read(cache_entry, buf, offset, size);

transcoder_fail:
passthrough:
open_fail:
    free(origpath);
translate_fail:
    if (read >= 0)
    {
        return (int)read;
    }
    else
    {
        return -errno;
    }
}

static int ffmpegfs_statfs(const char *path, struct statvfs *stbuf)
{
    char* origpath;

    ffmpegfs_trace("statfs %s", path);

    errno = 0;

    origpath = translate_path(path);
    if (!origpath)
    {
        goto translate_fail;
    }

    /* pass-through for regular files */
    if (statvfs(origpath, stbuf) == 0)
    {
        goto passthrough;
    }
    else
    {
        /* Not really an error. */
        errno = 0;
    }

    find_original(origpath);

    statvfs(origpath, stbuf);

    errno = 0;  // Just to make sure - reset any error

passthrough:
    free(origpath);
translate_fail:
    return -errno;
}

static int ffmpegfs_release(const char *path, struct fuse_file_info *fi)
{
    struct Cache_Entry* cache_entry;

    ffmpegfs_trace("release %s", path);

    cache_entry = (struct Cache_Entry*)fi->fh;
    if (cache_entry)
    {
        transcoder_delete(cache_entry);
    }

    return 0;
}

static void handler(int sig, __attribute__((unused)) siginfo_t *si, __attribute__((unused)) void *uc)
{
    if (sig == SIG)
    {
        transcoder_cache_maintenance();
    }
}

static int start_maintenance_timer(time_t interval)
{
    struct sigevent sev;
    struct itimerspec its;
    long long freq_nanosecs;
    struct sigaction sa;
    char maintenance_timer[100];

    format_time(maintenance_timer, sizeof(maintenance_timer), interval);

    freq_nanosecs = interval * 1000000000LL;

    ffmpegfs_info("Starting maintenance timer with %speriod.", maintenance_timer);

    // Establish handler for timer signal
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIG, &sa, NULL) == -1)
    {
        ffmpegfs_error("start_maintenance_timer() sigaction failed: %s", strerror(errno));
        return -1;
    }

    // Block timer signal temporarily
    sigemptyset(&mask);
    sigaddset(&mask, SIG);
    if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1)
    {
        ffmpegfs_error("start_maintenance_timer() sigprocmask failed: %s", strerror(errno));
        return -1;
    }

    // Create the timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1)
    {
        ffmpegfs_error("start_maintenance_timer() timer_create failed: %s", strerror(errno));
        return -1;
    }

    // Start the timer
    its.it_value.tv_sec = freq_nanosecs / 1000000000;
    its.it_value.tv_nsec = freq_nanosecs % 1000000000;
    its.it_interval.tv_sec = its.it_value.tv_sec;
    its.it_interval.tv_nsec = its.it_value.tv_nsec;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
    {
        ffmpegfs_error("timer_settime() timer_create failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static void *ffmpegfs_init(struct fuse_conn_info *conn)
{
    ffmpegfs_info("%s V%s initialising", PACKAGE_NAME, PACKAGE_VERSION);

    // We need synchronous reads.
    conn->async_read = 0;

    if (params.m_cache_maintenance)
    {
        start_maintenance_timer(params.m_cache_maintenance);
    }

    return NULL;
}

static void ffmpegfs_destroy(__attribute__((unused)) void * p)
{
    ffmpegfs_info("%s V%s terminating", PACKAGE_NAME, PACKAGE_VERSION);
    printf("%s V%s terminating\n", PACKAGE_NAME, PACKAGE_VERSION);

    transcoder_exit();

    ffmpegfs_trace("Unblocking signal %d\n", SIG);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
    {
        ffmpegfs_error("timer_settime() sigprocmask failed: %s", strerror(errno));
    }

    cache_delete();

    ffmpegfs_debug("%s V%s terminated", PACKAGE_NAME, PACKAGE_VERSION);
}

struct fuse_operations ffmpegfs_ops =
{
    .getattr  = ffmpegfs_getattr,
    .fgetattr = ffmpegfs_fgetattr,
    .readlink = ffmpegfs_readlink,
    .readdir  = ffmpegfs_readdir,
    .open     = ffmpegfs_open,
    .read     = ffmpegfs_read,
    .statfs   = ffmpegfs_statfs,
    .release  = ffmpegfs_release,
    .init     = ffmpegfs_init,
    .destroy  = ffmpegfs_destroy,
};
