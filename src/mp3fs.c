/*
 * MP3FS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC and Ogg Vorbis) to MP3 on the fly when opened and read.
 * See README for more details.
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2012 K. Henriksson
 * FFMPEG supplementals (c) 2017 by Norbert Schlia (nschlia@oblivion-software.de)
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

    .desttype           	= "mp4",
#ifndef DISABLE_ISMV
    .enable_ismv			= 0,
#endif

    .audiobitrate       	= 128,
    .audiosamplerate      	= 44100,

    .videobitrate       	= 2000,
#ifndef DISABLE_AVFILTER
    .deinterlace            = 0,
    .videowidth             = 0,
    .videoheight           	= 0,
#endif

    .debug              	= 0,
    .log_maxlevel       	= "INFO",
    .log_stderr         	= 0,
    .log_syslog         	= 0,
    .logfile            	= "",

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
    MP3FS_OPT("--desttype=%s",              desttype, 0),
    MP3FS_OPT("desttype=%s",                desttype, 0),
#ifndef DISABLE_ISMV
    MP3FS_OPT("--enable_ismv=%u",           enable_ismv, 0),
    MP3FS_OPT("enable_ismv=%u",             enable_ismv, 0),
#endif

    // Audio
    MP3FS_OPT("-b %u",                      audiobitrate, 0),
    MP3FS_OPT("--bitrate=%u",               audiobitrate, 0),
    MP3FS_OPT("bitrate=%u",                 audiobitrate, 0),
    MP3FS_OPT("--audiobitrate=%u",          audiobitrate, 0),
    MP3FS_OPT("audiobitrate=%u",            audiobitrate, 0),
    MP3FS_OPT("--audiosamplerate=%u",       audiosamplerate, 0),
    MP3FS_OPT("audiosamplerate=%u",         audiosamplerate, 0),

    // Video
    MP3FS_OPT("--videobitrate=%u",          videobitrate, 0),
    MP3FS_OPT("videobitrate=%u",            videobitrate, 0),
#ifndef DISABLE_AVFILTER
    MP3FS_OPT("--videoheight=%u",           videowidth, 0),
    MP3FS_OPT("videoheight=%u",             videowidth, 0),
    MP3FS_OPT("--videowidth=%u",            videoheight, 0),
    MP3FS_OPT("videowidth=%u",              videoheight, 0),
    MP3FS_OPT("--deinterlace=%u",           deinterlace, 0),
    MP3FS_OPT("deinterlace=%u",             deinterlace, 0),
#endif

    MP3FS_OPT("--expiry_time=%u",           expiry_time, 0),
    MP3FS_OPT("expiry_time=%u",             expiry_time, 0),
    MP3FS_OPT("--max_inactive_suspend=%u",  max_inactive_suspend, 0),
    MP3FS_OPT("max_inactive_suspend=%u",    max_inactive_suspend, 0),
    MP3FS_OPT("--max_inactive_abort=%u",    max_inactive_abort, 0),
    MP3FS_OPT("max_inactive_abort=%u",      max_inactive_abort, 0),
    MP3FS_OPT("--cachepath=%s",             cachepath, 0),
    MP3FS_OPT("cachepath=%s",               cachepath, 0),

    MP3FS_OPT("-d",                         debug, 1),
    MP3FS_OPT("debug",                      debug, 1),
    MP3FS_OPT("--log_maxlevel=%s",          log_maxlevel, 0),
    MP3FS_OPT("log_maxlevel=%s",            log_maxlevel, 0),
    MP3FS_OPT("--log_stderr",               log_stderr, 1),
    MP3FS_OPT("log_stderr",                 log_stderr, 1),
    MP3FS_OPT("--log_syslog",               log_syslog, 1),
    MP3FS_OPT("log_syslog",                 log_syslog, 1),
    MP3FS_OPT("--logfile=%s",               logfile, 0),
    MP3FS_OPT("logfile=%s",                 logfile, 0),

    FUSE_OPT_KEY("-h",                      KEY_HELP),
    FUSE_OPT_KEY("--help",                  KEY_HELP),
    FUSE_OPT_KEY("-V",                      KEY_VERSION),
    FUSE_OPT_KEY("--version",               KEY_VERSION),
    FUSE_OPT_KEY("-d",                      KEY_KEEP_OPT),
    FUSE_OPT_KEY("debug",                   KEY_KEEP_OPT),
    FUSE_OPT_END
};

#define INFO "Mount IN_DIR on OUT_DIR, converting audio/video files to MP3/MP4 upon access."

void usage(char *name) {
    printf("Usage: %s [OPTION]... IN_DIR OUT_DIR\n\n", name);
    fputs(INFO "\n"
               "\n"
               "Encoding options:\n"
               "\n"
               "    --desttype=TYPE, -odesttype=TYPE\n"
               "                           Select destination format. Can currently be\n"
               "                           either mp3 or mp4. To stream videos, mp4 must be\n"
               "                           selected.\n"
               "                           Default: mp4\n"
      #ifndef DISABLE_ISMV
               "    --enable_ismv=0|1, -oenable_ismv=0|1\n"
               "                           Set to 1 to create a ISMV (Smooth Streaming) file.\n"
               "                           Must be used together with desttype=mp4.\n"
               "                           Resulting files will stream to Internet Explorer, but\n"
               "                           are not compatible with most other players.\n"
               "                           Default: 0\n"
      #endif
               "\n"
               "Audio Options:\n"
               "\n"
               "    -b RATE, --audiobitrate=RATE, -oaudiobitrate=RATE\n"
               "                           Audio encoding bitrate (in kbit): Acceptable values for RATE\n"
               "                           include 96, 112, 128, 160, 192, 224, 256, and\n"
               "                           320.\n"
               "                           Default: 128 kbit\n"
               "    --audiosamplerate=Hz, -oaudiosamplerate=Hz\n"
               "                           Limits the output sample rate to Hz. If the source file\n"
               "                           sample rate is more it will be downsampled automatically.\n"
               "                           Typical values are 8000, 11025, 22050, 44100,\n"
               "                           48000, 96000, 192000. Set to 0 to keep source rate.\n"
               "                           Default: 44100 Hz\n"
               "\n"
               "Video Options:\n"
               "\n"
               "    --videobitrate=RATE, -ovideobitrate=RATE\n"
               "                           Video encoding bit rate (in kbit). Acceptable values for RATE\n"
               "                           range between 500 and 250000. Setting this too high or low may.\n"
               "                           cause transcoding to fail.\n"
               "                           Default: 2000 kbit\n"
      #ifndef DISABLE_AVFILTER
               "    --videoheight=HEIGHT, -ovideoheight=HEIGHT\n"
               "                           Sets the height of the target video.\n"
               "                           When the video is rescaled the aspect ratio is\n"
               "                           preserved if --width is not set at the same time.\n"
               "                           Default: keep source video height\n"
               "    --videowidth=WIDTH, -ovideowidth=WIDTH\n"
               "                           Sets the width of the target video.\n"
               "                           Whene the video is rescaled the aspect ratio is\n"
               "                           preserved if --height is not set at the same time.\n"
               "                           Default: keep source video width\n"
               "    --deinterlace=0|1, -deinterlace=0|1\n"
               "                           Deinterlace video if necessary while transcoding.\n"
               "                           May need higher bit rate, but will increase picture qualitiy\n"
               "                           when streaming via HTML5.\n"
               "                           Default: 0\n"
      #endif
               "\n"
               "Cache Options:\n"
               "\n"
               "     --expiry_time=SECONDS, -o expiry_time=SECONDS\n"
               "                           Cache entries expire after SECONDS and will be deleted\n"
               "                           to save disk space.\n"
               "                           Default: 1 week\n"
               "     --max_inactive_suspend=SECONDS, -o max_inactive_suspend=SECONDS\n"
               "                           While being accessed the file is transcoded to the target format\n"
               "                           in the background. When the client quits transcoding will continue\n"
               "                           until this time out. Transcoding is suspended until it is\n"
               "                           accessed again, and transcoding will continue.\n"
               "                           Default: 2 minutes\n"
               "     --max_inactive_abort=SECONDS, -o max_inactive_abort=SECONDS\n"
               "                           While being accessed the file is transcoded to the target format\n"
               "                           in the background. When the client quits transcoding will continue\n"
               "                           until this time out, and the transcoder thread quits\n"
               "                           Default: 5 minutes\n"
               "     --cachepath=DIR, -o cachepath=DIR\n"
               "                           Sets the disk cache directory to DIR. Will be created if not existing.\n"
               "                           The user running mp3fs must have write access to the location.\n"
               "                           Default: temp directory, e.g. /tmp\n"
               "\n"
               "Logging:\n"
               "\n"
               "    --log_maxlevel=LEVEL, -olog_maxlevel=LEVEL\n"
               "                           Maximum level of messages to log, either ERROR,\n"
               "                           INFO, TRACE or DEBUG. Defaults to INFO, and always set\n"
               "                           to DEBUG in debug mode. Note that the other log\n"
               "                           flags must also be set to enable logging.\n"
               "    --log_stderr, -olog_stderr\n"
               "                           Enable outputting logging messages to stderr.\n"
               "                           Enabled in debug mode.\n"
               "    --log_syslog, -olog_syslog\n"
               "                           Enable outputting logging messages to syslog.\n"
               "    --logfile=FILE, -ologfile=FILE\n"
               "                           File to output log messages to. By default, no\n"
               "                           file will be written.\n"
               "\n"
               "General options:\n"
               "    -h, --help             display this help and exit\n"
               "    -V, --version          output version information and exit\n"
               "\n", stdout);
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
        printf("%-20s: %s\n", PACKAGE_NAME " Version", PACKAGE_VERSION);

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
    char cachepath[PATH_MAX];
    int ret;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    printf("%s V%s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Copyright (C) 2006-2008 David Collett\n"
           "Copyright (C) 2008-2012 K. Henriksson\n"
           "FFMPEG supplementals (c) 2017 by Norbert Schlia (nschlia@oblivion-software.de)\n\n");

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
        fprintf(stderr, "ERROR: parsing options.\n\n");
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

    cache_path(cachepath, sizeof(cachepath));

    mp3fs_debug(PACKAGE_NAME " options:\n\n"
                             "basepath:           %s\n"
                             "mountpath:          %s\n"
                             "desttype:           %s\n"
            #ifndef DISABLE_ISMV
                             "use ISMV:           %s\n"
            #endif
                             "audio bitrate:      %u\n"
                             "audio sample rate:  %u\n"
            #ifndef DISABLE_AVFILTER
                             "video size:         %ux%u\n"
            #endif
                             "video bitrate:      %u\n"
                             "log_maxlevel:       %s\n"
                             "log_stderr:         %u\n"
                             "log_syslog:         %u\n"
                             "logfile:            %s\n"
                             "cache settings:\n"
                             "cache path:         %s\n"
                             "expiry:             %zu seconds\n"
                             "inactivity suspend: %zu seconds\n"
                             "inactivity abort:   %zu seconds\n",
                params.basepath,
                params.mountpath,
                params.desttype,
            #ifndef DISABLE_ISMV
                params.enable_ismv ? "yes" : "no",
            #endif
                params.audiobitrate,
                params.audiosamplerate,
            #ifndef DISABLE_AVFILTER
                params.videowidth,
                params.videoheight,
            #endif
                params.videobitrate,
                params.log_maxlevel,
                params.log_stderr,
                params.log_syslog,
                params.logfile,
                cachepath,
                params.expiry_time,
                params.max_inactive_suspend,
                params.max_inactive_abort);

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &mp3fs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
