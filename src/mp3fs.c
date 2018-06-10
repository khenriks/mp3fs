/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC and Ogg Vorbis) to MP3 on the fly when opened and read.
 * See README for more details.
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transcode.h"

struct mp3fs_params params = {
    .basepath        = NULL,
    .bitrate         = 128,
    .debug           = 0,
    .gainmode        = 1,
    .gainref         = 89.0,
    .log_maxlevel    = "INFO",
    .log_stderr      = 0,
    .log_syslog      = 0,
    .logfile         = "",
    .quality         = 5,
    .statcachesize   = 0,
    .vbr             = 0,
#ifdef HAVE_MP3
    .desttype  = "mp3",
#endif
};

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_KEEP_OPT
};

#define MP3FS_OPT(t, p, v) { t, offsetof(struct mp3fs_params, p), v }

static struct fuse_opt mp3fs_opts[] = {
    MP3FS_OPT("-b %u",                bitrate, 0),
    MP3FS_OPT("bitrate=%u",           bitrate, 0),
    MP3FS_OPT("-d",                   debug, 1),
    MP3FS_OPT("debug",                debug, 1),
    MP3FS_OPT("--desttype=%s",        desttype, 0),
    MP3FS_OPT("desttype=%s",          desttype, 0),
    MP3FS_OPT("--gainmode=%d",        gainmode, 0),
    MP3FS_OPT("gainmode=%d",          gainmode, 0),
    MP3FS_OPT("--gainref=%f",         gainref, 0),
    MP3FS_OPT("gainref=%f",           gainref, 0),
    MP3FS_OPT("--log_maxlevel=%s",    log_maxlevel, 0),
    MP3FS_OPT("log_maxlevel=%s",      log_maxlevel, 0),
    MP3FS_OPT("--log_stderr",         log_stderr, 1),
    MP3FS_OPT("log_stderr",           log_stderr, 1),
    MP3FS_OPT("--log_syslog",         log_syslog, 1),
    MP3FS_OPT("log_syslog",           log_syslog, 1),
    MP3FS_OPT("--logfile=%s",         logfile, 0),
    MP3FS_OPT("logfile=%s",           logfile, 0),
    MP3FS_OPT("--quality=%u",         quality, 0),
    MP3FS_OPT("quality=%u",           quality, 0),
    MP3FS_OPT("--statcachesize=%u",   statcachesize, 0),
    MP3FS_OPT("statcachesize=%u",     statcachesize, 0),
    MP3FS_OPT("--vbr",                vbr, 1),
    MP3FS_OPT("vbr",                  vbr, 1),

    FUSE_OPT_KEY("-h",                KEY_HELP),
    FUSE_OPT_KEY("--help",            KEY_HELP),
    FUSE_OPT_KEY("-V",                KEY_VERSION),
    FUSE_OPT_KEY("--version",         KEY_VERSION),
    FUSE_OPT_KEY("-d",                KEY_KEEP_OPT),
    FUSE_OPT_KEY("debug",             KEY_KEEP_OPT),
    FUSE_OPT_END
};

void usage(char *name) {
    printf("Usage: %s [OPTION]... IN_DIR OUT_DIR\n", name);
    fputs("\
Mount IN_DIR on OUT_DIR, converting FLAC/Ogg Vorbis files to MP3 upon access.\n\
\n\
Encoding options:\n\
    -b RATE, -obitrate=RATE\n\
                           encoding bitrate: Acceptable values for RATE\n\
                           include 96, 112, 128, 160, 192, 224, 256, and\n\
                           320; 128 is the default\n\
    --gainmode=<0,1,2>, -ogainmode=<0,1,2>\n\
                           what to do with ReplayGain tags:\n\
                           0 - ignore, 1 - prefer album gain (default),\n\
                           2 - prefer track gain\n\
    --gainref=REF, -ogainref=REF\n\
                           reference value to use for ReplayGain in \n\
                           decibels: defaults to 89 dB\n\
    --log_maxlevel=LEVEL, -olog_maxlevel=LEVEL\n\
                           maximum level of messages to log, either ERROR,\n\
                           INFO, or DEBUG. Defaults to INFO, and always set\n\
                           to DEBUG in debug mode. Note that the other log\n\
                           flags must also be set to enable logging\n\
    --log_stderr, -olog_stderr\n\
                           enable outputting logging messages to stderr.\n\
                           Enabled in debug mode.\n\
    --log_syslog, -olog_syslog\n\
                           enable outputting logging messages to syslog\n\
    --logfile=FILE, -ologfile=FILE\n\
                           file to output log messages to. By default, no\n\
                           file will be written.\n\
    --quality=<0..9>, -oquality=<0..9>\n\
                           encoding quality: 0 is slowest, 9 is fastest;\n\
                           5 is the default\n\
    --statcachesize=SIZE, -ostatcachesize=SIZE\n\
                           Set the number of entries for the file stats\n\
                           cache.  Necessary for decent performance when\n\
                           VBR is enabled.  Each entry takes 100-200 bytes.\n\
    --vbr, -ovbr           Use variable bit rate encoding.  When set, the\n\
                           bit rate set with '-b' sets the maximum bit rate.\n\
                           Performance will be terrible unless the\n\
                           statcachesize is enabled.\n\
\n\
General options:\n\
    -h, --help             display this help and exit\n\
    -V, --version          output version information and exit\n\
\n", stdout);
}

static int mp3fs_opt_proc(void* data, const char* arg, int key,
                          struct fuse_args *outargs) {
    (void)data;
    switch(key) {
        case FUSE_OPT_KEY_NONOPT:
            // check for flacdir and bitrate parameters
            if (!params.basepath) {
                params.basepath = arg;
                return 0;
            }
            break;

        case KEY_HELP:
            usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-ho");
            fuse_main(outargs->argc, outargs->argv, &mp3fs_ops, NULL);
            exit(1);

        case KEY_VERSION:
            // TODO: Also output this information in debug mode
            printf("mp3fs version: %s\n", PACKAGE_VERSION);
            print_codec_versions();
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

    /* Log to the screen, and enable debug messages, if debug is enabled. */
    if (params.debug) {
        params.log_stderr = 1;
        params.log_maxlevel = "DEBUG";
    }

    if (!init_logging(params.logfile, params.log_maxlevel, params.log_stderr,
                      params.log_syslog)) {
        fprintf(stderr, "Failed to initialize logging module.\n");
        fprintf(stderr, "Maybe log file couldn't be opened for writing?\n");
        return 1;
    }

    if (!params.basepath) {
        fprintf(stderr, "No valid flacdir specified.\n\n");
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
        fprintf(stderr, "flacdir is not a valid directory: %s\n",
                params.basepath);
        fprintf(stderr, "Hint: Did you specify bitrate using the old "
                "syntax instead of the new -b?\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.quality > 9) {
        fprintf(stderr, "Invalid encoding quality value: %u\n\n",
                params.quality);
        usage(argv[0]);
        return 1;
    }

    /* Check for valid destination type. */
    if (!check_encoder(params.desttype)) {
        fprintf(stderr, "No encoder available for desttype: %s\n\n",
                params.desttype);
        usage(argv[0]);
        return 1;
    }

    mp3fs_debug("MP3FS options:\n"
                "basepath:       %s\n"
                "bitrate:        %u\n"
                "desttype:       %s\n"
                "gainmode:       %d\n"
                "gainref:        %f\n"
                "log_maxlevel:   %s\n"
                "log_stderr:     %u\n"
                "log_syslog:     %u\n"
                "logfile:        %s\n"
                "quality:        %u\n"
                "statcachesize:  %u\n"
                "vbr:            %u\n"
                ,
                params.basepath,
                params.bitrate,
                params.desttype,
                params.gainmode,
                params.gainref,
                params.log_maxlevel,
                params.log_stderr,
                params.log_syslog,
                params.logfile,
                params.quality,
                params.statcachesize,
                params.vbr);

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
