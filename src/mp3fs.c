/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC and Ogg Vorbis) to MP3 on the fly when opened and read.
 * See README for more details.
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2012 K. Henriksson
 * FFMPEG supplementals (c) 2017 by Norbert Schlia (nschlia@oblivon-software.de)
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transcode.h"

// TODO: Move this elsewehere, so this file can be library agnostic
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#pragma GCC diagnostic pop

#include "ffmpeg_utils.h"

struct mp3fs_params params = {
    .basepath           	= NULL,
    .mountpath          	= NULL,
    .width              	= 0,
    .maxwidth           	= 0,
    .height             	= 0,
    .maxheight          	= 0,
    .audiobitrate       	= 128,
    .maxaudiobitrate    	= 0,
    .videobitrate       	= 1000,
    .maxvideobitrate    	= 0,
    .debug              	= 0,
    .log_maxlevel       	= "INFO",
    .log_stderr         	= 0,
    .log_syslog         	= 0,
    .logfile            	= "",
    .statcachesize      	= 500,
    .maxsamplerate      	= 44100,
    .desttype           	= "mp4",
    .enable_ismv			= 0,
    .expiry_time            = (60*60*24 /* d */) * 7,	// default: 1 week
    .max_inactive_suspend   = (60 /* m */) * 2,         // default: 2 minutes
    .max_inactive_abort     = (60 /* m */) * 25,        // default: 5 minutes
};

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_KEEP_OPT
};

#define MP3FS_OPT(t, p, v) { t, offsetof(struct mp3fs_params, p), v }

static struct fuse_opt mp3fs_opts[] = {
    MP3FS_OPT("-h %u",              width, 0),
    MP3FS_OPT("height=%u",          width, 0),
    MP3FS_OPT("-w %u",              height, 0),
    MP3FS_OPT("width=%u",           height, 0),
    MP3FS_OPT("-b %u",              audiobitrate, 0),
    MP3FS_OPT("bitrate=%u",         audiobitrate, 0),
    MP3FS_OPT("--desttype=%s",      desttype, 0),
    MP3FS_OPT("desttype=%s",        desttype, 0),
    MP3FS_OPT("--enable_ismv=%s",   enable_ismv, 0),
    MP3FS_OPT("enable_ismv=%s",     enable_ismv, 0),
    MP3FS_OPT("--maxsamplerate=%u", maxsamplerate, 0),
    MP3FS_OPT("maxsamplerate=%u",   maxsamplerate, 0),
    MP3FS_OPT("--statcachesize=%u", statcachesize, 0),
    MP3FS_OPT("statcachesize=%u",   statcachesize, 0),

    MP3FS_OPT("-d",                 debug, 1),
    MP3FS_OPT("debug",              debug, 1),
    MP3FS_OPT("--log_maxlevel=%s",  log_maxlevel, 0),
    MP3FS_OPT("log_maxlevel=%s",    log_maxlevel, 0),
    MP3FS_OPT("--log_stderr",       log_stderr, 1),
    MP3FS_OPT("log_stderr",         log_stderr, 1),
    MP3FS_OPT("--log_syslog",       log_syslog, 1),
    MP3FS_OPT("log_syslog",         log_syslog, 1),
    MP3FS_OPT("--logfile=%s",       logfile, 0),
    MP3FS_OPT("logfile=%s",         logfile, 0),

    FUSE_OPT_KEY("-h",               KEY_HELP),
    FUSE_OPT_KEY("--help",           KEY_HELP),
    FUSE_OPT_KEY("-V",               KEY_VERSION),
    FUSE_OPT_KEY("--version",        KEY_VERSION),
    FUSE_OPT_KEY("-d",               KEY_KEEP_OPT),
    FUSE_OPT_KEY("debug",            KEY_KEEP_OPT),
    FUSE_OPT_END
};

#define INFO "Mount IN_DIR on OUT_DIR, converting audio/video files to MP3/MP4 upon access."

void usage(char *name) {
    printf("Usage: %s [OPTION]... IN_DIR OUT_DIR\n\n", name);
    fputs(INFO "\n\
          \n\
          Encoding options:\n\
              -h HEIGHT, --height=HEIGHT\n\
                             Video only: sets the height of the target video.\n\
                             When the video is rescaled the aspect ratio is\n\
                             preserved if --width is not set at the same time.\n\
                             Default: keep source video height\n\
              -w WIDTH, --width=WIDTH\n\
                             Video only: sets the width of the target video.\n\
                             Whene the video is rescaled the aspect ratio is\n\
                             preserved if --height is not set at the same time.\n\
                             Default: keep source video width\n\
              -b RATE, -obitrate=RATE\n\
                             encoding bitrate: Acceptable values for RATE\n\
                             include 96, 112, 128, 160, 192, 224, 256, and\n\
                             320.\n\
                             Default: 128\n\
              --desttype=TYPE, -odesttype=TYPE\n\
                             Select destination format. Can currently be\n\
                             either mp3 or mp4. To stream videos, mp4 must be\n\
                             selected.\n\
                             Default: mp4\n\
              --enable_ismv=0|1, -oenable_ismv=0|1\n\
                             Set to 1 to create a ISMV (Smooth Streaming) file.\n\
                             Must be used together with desttype=mp4.\n\
                             Resulting files will stream to Internet Explorer, but\n\
                             are not compatible with most other players.\n\
                             Default: 0\n\
    		  --maxsamplerate=Hz, -omaxsamplerate=Hz\n\
                             Limits the output sample rate to Hz. If the source file\n\
                             sample rate is more it will be downsampled automatically.\n\
                             Typical values are 8000, 11025, 22050, 44100,\n\
                             48000, 96000, 192000\n\
                             Default: keep source rate\n\
              --statcachesize=SIZE, -ostatcachesize=SIZE\n\
                             Set the number of entries for the file stats\n\
                             cache. Each entry takes 100-200 bytes.\n\
                             Default: 500\n\
              --log_maxlevel=LEVEL, -olog_maxlevel=LEVEL\n\
                             Maximum level of messages to log, either ERROR,\n\
                             INFO, TRACE or DEBUG. Defaults to INFO, and always set\n\
                             to DEBUG in debug mode. Note that the other log\n\
                             flags must also be set to enable logging.\n\
              --log_stderr, -olog_stderr\n\
                             Enable outputting logging messages to stderr.\n\
                             Enabled in debug mode.\n\
              --log_syslog, -olog_syslog\n\
                             Enable outputting logging messages to syslog.\n\
              --logfile=FILE, -ologfile=FILE\n\
                             File to output log messages to. By default, no\n\
                             file will be written.\n\
\n\
General options:\n\
              -h, --help     display this help and exit\n\
              -V, --version  output version information and exit\n\
\n", stdout);
}

static int mp3fs_opt_proc(void* data, const char* arg, int key, struct fuse_args *outargs) {
    static int n;
    (void)data;
    switch(key) {
    case FUSE_OPT_KEY_NONOPT:
    {
        // check for basepath and bitrate parameters
        if (n == 0 && !params.basepath) {
            params.basepath = arg;
            n++;
            return 0;
        }
        else if (n == 1 && !params.mountpath) {
            params.mountpath = arg;
            n++;
            return 1;
        }

        break;
    }
    case KEY_HELP:
    {
        usage(outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &mp3fs_ops, NULL);
        exit(1);
    }
    case KEY_VERSION:
    {
        // TODO: Also output this information in debug mode
        printf("%-20s: %s\n", PACKAGE_NAME " version", PACKAGE_VERSION);

        char buffer[1024];
        ffmpeg_libinfo(buffer, sizeof(buffer));
        printf("%s", buffer);

        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &mp3fs_ops, NULL);
        exit(0);
    }
    }

    return 1;
}

void cleanup()
{
    cache_delete();
}

int main(int argc, char *argv[]) {
    int ret;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    /* register the termination function */
    atexit(cleanup);

    cache_new();

    // Configure FFMPEG
    /* register all the codecs */
    avcodec_register_all();
    av_register_all();
    //show_formats_devices(0);
#ifndef USING_LIBAV
    // Redirect FFMPEG logs
    av_log_set_callback(ffmpeg_log);
#endif

    if (fuse_opt_parse(&args, &params, mp3fs_opts, mp3fs_opt_proc)) {
        fprintf(stderr, "Error parsing options.\n\n");
        usage(argv[0]);
        return 1;
    }

    /* Log to the screen, and enable debug messages, if debug is enabled. */
    if (params.debug) {
        params.log_stderr = 1;
        params.log_maxlevel = "DEBUG";
        //        av_log_set_level(AV_LOG_DEBUG);
        av_log_set_level(AV_LOG_INFO);
    }
    else
    {
        av_log_set_level(AV_LOG_QUIET);
    }

    if (!init_logging(params.logfile, params.log_maxlevel, params.log_stderr, params.log_syslog)) {
        fprintf(stderr, "ERROR: Failed to initialise logging module.\n");
        fprintf(stderr, "Maybe log file couldn't be opened for writing?\n\n");
        return 1;
    }

    if (!params.basepath) {
        fprintf(stderr, "ERROR: No valid basepath specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.basepath[0] != '/') {
        fprintf(stderr, "ERROR: basepath must be an absolute path.\n\n");
        usage(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(params.basepath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ERROR: basepath is not a valid directory: %s\n\n", params.basepath);
        usage(argv[0]);
        return 1;
    }

    if (!params.mountpath) {
        fprintf(stderr, "ERROR: No valid mountpath specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.mountpath[0] != '/') {
        fprintf(stderr, "ERROR: mountpath must be an absolute path.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (stat(params.mountpath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "ERROR: mountpath is not a valid directory: %s\n\n", params.mountpath);
        usage(argv[0]);
        return 1;
    }

    /* Check for valid destination type. */
    if (!check_encoder(params.desttype)) {
        fprintf(stderr, "ERROR: No encoder available for desttype: %s\n\n", params.desttype);
        usage(argv[0]);
        return 1;
    }

    mp3fs_debug(PACKAGE_NAME " options:\n\n"
                "basepath:        %s\n"
                "mountpath:       %s\n"
                "video width:   %2s%u\n"
                "video height:  %2s%u\n"
                "audio bitrate: %2s%u\n"
                "video bitrate: %2s%u\n"
                "desttype:        %s\n"
                "log_maxlevel:    %s\n"
                "log_stderr:      %u\n"
                "log_syslog:      %u\n"
                "logfile:         %s\n"
                "statcachesize:   %u\n",
                params.basepath,
                params.mountpath,
                params.maxwidth ? "<=" : " =", params.width,
                params.maxheight ? "<=" : " =", params.height,
                params.maxaudiobitrate ? "<=" : "", params.audiobitrate,
                params.maxvideobitrate ? "<=" : "", params.videobitrate,
                params.desttype,
                params.log_maxlevel,
                params.log_stderr,
                params.log_syslog,
                params.logfile,
                params.statcachesize);

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
