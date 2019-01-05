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

#include <iostream>

#define FUSE_USE_VERSION 26

#include <fuse.h>
#ifdef __APPLE__
#include <fuse_darwin.h>
#endif

#define FUSE_USE_VERSION 26

#include <fuse.h>

#include "codecs/coders.h"
#include "logging.h"
#include "mp3fs.h"

/* Fuse operations struct */
extern struct fuse_operations mp3fs_ops;

namespace {

enum {
    KEY_HELP,
    KEY_VERSION,
    KEY_KEEP_OPT
};

#define MP3FS_OPT(t, p, v) { t, offsetof(struct mp3fs_params, p), v }

struct fuse_opt mp3fs_opts[] = {
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

void usage(const std::string& name) {
    std::cout << "Usage: " << name << " [OPTION]... IN_DIR OUT_DIR" <<
        std::endl;
    std::cout << R"(
Mount IN_DIR on OUT_DIR, converting FLAC/Ogg Vorbis files to MP3 upon access.

Encoding options:
    -b RATE, -obitrate=RATE
                           encoding bitrate: Acceptable values for RATE
                           include 96, 112, 128, 160, 192, 224, 256, and
                           320; 128 is the default
    --gainmode=<0,1,2>, -ogainmode=<0,1,2>
                           what to do with ReplayGain tags:
                           0 - ignore, 1 - prefer album gain (default),
                           2 - prefer track gain
    --gainref=REF, -ogainref=REF
                           reference value to use for ReplayGain in
                           decibels: defaults to 89 dB
    --log_maxlevel=LEVEL, -olog_maxlevel=LEVEL
                           maximum level of messages to log, either ERROR,
                           INFO, or DEBUG. Defaults to INFO, and always set
                           to DEBUG in debug mode. Note that the other log
                           flags must also be set to enable logging
    --log_stderr, -olog_stderr
                           enable outputting logging messages to stderr.
                           Enabled in debug mode.
    --log_syslog, -olog_syslog
                           enable outputting logging messages to syslog
    --logfile=FILE, -ologfile=FILE
                           file to output log messages to. By default, no
                           file will be written.
    --quality=<0..9>, -oquality=<0..9>
                           encoding quality: 0 is slowest, 9 is fastest;
                           5 is the default
    --statcachesize=SIZE, -ostatcachesize=SIZE
                           Set the number of entries for the file stats
                           cache.  Necessary for decent performance when
                           VBR is enabled.  Each entry takes 100-200 bytes.
    --vbr, -ovbr           Use variable bit rate encoding.  When set, the
                           bit rate set with '-b' sets the maximum bit rate.
                           Performance will be terrible unless the
                           statcachesize is enabled.

General options:
    -h, --help             display this help and exit
    -V, --version          output version information and exit
)" << std::endl;
}

void print_versions(std::ostream&& out) {
#ifdef GIT_VERSION
    out << "mp3fs git version: " << GIT_VERSION << std::endl;
#else
    out << "mp3fs version: " << PACKAGE_VERSION << std::endl;
#endif
    print_codec_versions(out);
    out << "FUSE library version: " << FUSE_MAJOR_VERSION << "." <<
        FUSE_MINOR_VERSION << std::endl;
#ifdef __APPLE__
    out << "OS X FUSE version: " << osxfuse_version() << std::endl;
#endif
}

int mp3fs_opt_proc(void*, const char* arg, int key, struct fuse_args *outargs) {
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
            print_versions(std::move(std::cout));
            exit(0);
    }

    return 1;
}

}  // namespace

struct mp3fs_params params = {
    .basepath        = NULL,
    .bitrate         = 128,
    .debug           = 0,
#ifdef HAVE_MP3
    .desttype        = "mp3",
#endif
    .gainmode        = 1,
    .gainref         = 89.0,
    .log_maxlevel    = "INFO",
    .log_stderr      = 0,
    .log_syslog      = 0,
    .logfile         = "",
    .quality         = 5,
    .statcachesize   = 0,
    .vbr             = 0,
};

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &params, mp3fs_opts, mp3fs_opt_proc)) {
        std::cerr << "Error parsing options.\n" << std::endl;
        usage(argv[0]);
        return 1;
    }

    /* Log to the screen, and enable debug messages, if debug is enabled. */
    if (params.debug) {
        params.log_stderr = 1;
        params.log_maxlevel = "DEBUG";
    }

    if (!InitLogging(params.logfile, StringToLevel(params.log_maxlevel),
                     params.log_stderr, params.log_syslog)) {
        std::cerr << "Failed to initialize logging module." << std::endl;
        std::cerr << "Maybe log file couldn't be opened for writing?" <<
            std::endl;
        return 1;
    }

    if (!params.basepath) {
        std::cerr << "No valid flacdir specified.\n" << std::endl;
        usage(argv[0]);
        return 1;
    }

    if (params.basepath[0] != '/') {
        std::cerr << "flacdir must be an absolute path.\n" << std::endl;
        usage(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(params.basepath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::cerr << "flacdir is not a valid directory: " << params.basepath <<
            std::endl;
        std::cerr << "Hint: Did you specify bitrate using the old "
            "syntax instead of the new -b?\n" << std::endl;
        usage(argv[0]);
        return 1;
    }

    if (params.quality > 9) {
        std::cerr << "Invalid encoding quality value: " << params.quality <<
            std::endl << std::endl;
        usage(argv[0]);
        return 1;
    }

    /* Check for valid destination type. */
    if (!check_encoder(params.desttype)) {
        std::cerr << "No encoder available for desttype: " << params.desttype <<
            std::endl << std::endl;
        usage(argv[0]);
        return 1;
    }

    print_versions(Log(DEBUG));

    Log(DEBUG) << "MP3FS options:" << std::endl
               << "basepath:       " << params.basepath << std::endl
               << "bitrate:        " << params.bitrate << std::endl
               << "desttype:       " << params.desttype << std::endl
               << "gainmode:       " << params.gainmode << std::endl
               << "gainref:        " << params.gainref << std::endl
               << "log_maxlevel:   " << params.log_maxlevel << std::endl
               << "log_stderr:     " << params.log_stderr << std::endl
               << "log_syslog:     " << params.log_syslog << std::endl
               << "logfile:        " << params.logfile << std::endl
               << "quality:        " << params.quality << std::endl
               << "statcachesize:  " << params.statcachesize << std::endl
               << "vbr:            " << params.vbr;

    // start FUSE
    int ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
