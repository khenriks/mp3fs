/*
 * FFMPEGFS: A read-only FUSE filesystem which transcodes audio formats
 * (currently FLAC and Ogg Vorbis) to MP3 on the fly when opened and read.
 * See README for more details.
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <regex.h>

#include "transcode.h"

// TODO: Move this elsewehere, so this file can be library agnostic
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#ifdef __GNUC__
#  include <features.h>
#  if __GNUC_PREREQ(5,0)
// GCC >= 5.0
#     pragma GCC diagnostic ignored "-Wfloat-conversion"
#  elif __GNUC_PREREQ(4,8)
// GCC >= 4.8
#  else
#     error("GCC < 4.8 not supported");
#  endif
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#pragma GCC diagnostic pop

#include "ffmpeg_utils.h"

struct ffmpegfs_params params =
{
    .m_basepath           	= NULL,                     // required parameter
    .m_mountpath          	= NULL,                     // required parameter

    .m_desttype           	= "mp4",                    // default: encode to mp4
#ifndef DISABLE_ISMV
    .m_enable_ismv          = 0,                        // default: do not use ISMV
#endif

    .m_audiobitrate       	= 128000,                   // default: 128 kBit
    .m_audiosamplerate      = 44100,                    // default: 44.1 kHz

    .m_videobitrate       	= 2000000,                  // default: 2 MBit
#ifndef DISABLE_AVFILTER
    .m_videowidth           = 0,                        // default: do not change width
    .m_videoheight          = 0,                        // default: do not change height
    .m_deinterlace          = 0,                        // default: do not interlace video
#endif

    .m_debug              	= 0,                        // default: no debug messages
    .m_log_maxlevel       	= "WARNING",                // default: WARNING level
    .m_log_stderr         	= 0,                        // default: do not log to stderr
    .m_log_syslog         	= 0,                        // default: do not use syslog
    .m_logfile            	= "",                       // default: none

    .m_expiry_time          = (60*60*24 /* d */) * 7,	// default: 1 week
    .m_max_inactive_suspend = (60 /* m */) * 2,         // default: 2 minutes
    .m_max_inactive_abort   = (60 /* m */) * 5,         // default: 5 minutes
    .m_max_cache_size       = 0,                        // default: no limit
    .m_min_diskspace        = 0,                        // default: no minimum
#ifndef DISABLE_MAX_THREADS
    .m_max_threads          = 0,                        // default: 4 * cpu cores (set later)
#endif
    .m_cachepath            = NULL,                     // default: /tmp
    .m_disable_cache        = 0                         // default: enabled
};

enum
{
    KEY_HELP,
    KEY_VERSION,
    KEY_KEEP_OPT,
    // Intelligent parameters
    KEY_AUDIO_BITRATE,
    KEY_AUDIO_SAMPLERATE,
    KEY_VIDEO_BITRATE,
    KEY_EXPIRY_TIME,
    KEY_MAX_INACTIVE_SUSPEND_TIME,
    KEY_MAX_INACTIVE_ABORT_TIME,
    KEY_MAX_CACHE_SIZE,
    KEY_MIN_DISKSPACE_SIZE
};

#define FFMPEGFS_OPT(templ, param, value) { templ, offsetof(struct ffmpegfs_params, param), value }

static struct fuse_opt ffmpegfs_opts[] =
{
    FFMPEGFS_OPT("--desttype=%s",               m_desttype, 0),
    FFMPEGFS_OPT("desttype=%s",                 m_desttype, 0),
#ifndef DISABLE_ISMV
    FFMPEGFS_OPT("--enable_ismv=%u",            m_enable_ismv, 0),
    FFMPEGFS_OPT("enable_ismv=%u",              m_enable_ismv, 0),
#endif

    // Audio
    FUSE_OPT_KEY("--audiobitrate=%s",           KEY_AUDIO_BITRATE),
    FUSE_OPT_KEY("audiobitrate=%s",             KEY_AUDIO_BITRATE),
    FUSE_OPT_KEY("--audiosamplerate=%u",        KEY_AUDIO_SAMPLERATE),
    FUSE_OPT_KEY("audiosamplerate=%u",          KEY_AUDIO_SAMPLERATE),

    // Video
    FUSE_OPT_KEY("--videobitrate=%u",           KEY_VIDEO_BITRATE),
    FUSE_OPT_KEY("videobitrate=%u",             KEY_VIDEO_BITRATE),
#ifndef DISABLE_AVFILTER
    FFMPEGFS_OPT("--videoheight=%u",            m_videowidth, 0),
    FFMPEGFS_OPT("videoheight=%u",              m_videowidth, 0),
    FFMPEGFS_OPT("--videowidth=%u",             m_videoheight, 0),
    FFMPEGFS_OPT("videowidth=%u",               m_videoheight, 0),
    FFMPEGFS_OPT("--deinterlace",               m_deinterlace, 1),
    FFMPEGFS_OPT("deinterlace",                 m_deinterlace, 1),
#endif

    // Cache
    FUSE_OPT_KEY("--expiry_time=%zu",           KEY_EXPIRY_TIME),
    FUSE_OPT_KEY("expiry_time=%zu",             KEY_EXPIRY_TIME),
    FUSE_OPT_KEY("--max_inactive_suspend=%zu",  KEY_MAX_INACTIVE_SUSPEND_TIME),
    FUSE_OPT_KEY("max_inactive_suspend=%zu",    KEY_MAX_INACTIVE_SUSPEND_TIME),
    FUSE_OPT_KEY("--max_inactive_abort=%zu",    KEY_MAX_INACTIVE_ABORT_TIME),
    FUSE_OPT_KEY("max_inactive_abort=%zu",      KEY_MAX_INACTIVE_ABORT_TIME),
    FUSE_OPT_KEY("--max_cache_size=%zu",        KEY_MAX_CACHE_SIZE),
    FUSE_OPT_KEY("max_cache_size=%zu",          KEY_MAX_CACHE_SIZE),
    FUSE_OPT_KEY("--min_diskspace=%zu",         KEY_MIN_DISKSPACE_SIZE),
    FUSE_OPT_KEY("min_diskspace=%zu",           KEY_MIN_DISKSPACE_SIZE),
    FFMPEGFS_OPT("--cachepath=%s",              m_cachepath, 0),
    FFMPEGFS_OPT("cachepath=%s",                m_cachepath, 0),
    FFMPEGFS_OPT("--disable_cache",             m_disable_cache, 1),
    FFMPEGFS_OPT("disable_cache",               m_disable_cache, 1),
    FFMPEGFS_OPT("--prune_cache",               m_prune_cache, 1),

    // Other
#ifndef DISABLE_MAX_THREADS
    FFMPEGFS_OPT("--max_threads=%u",            m_max_threads, 0),
    FFMPEGFS_OPT("max_threads=%u",              m_max_threads, 0),
#endif

    FFMPEGFS_OPT("-d",                          m_debug, 1),
    FFMPEGFS_OPT("debug",                       m_debug, 1),
    FFMPEGFS_OPT("--log_maxlevel=%s",           m_log_maxlevel, 0),
    FFMPEGFS_OPT("log_maxlevel=%s",             m_log_maxlevel, 0),
    FFMPEGFS_OPT("--log_stderr",                m_log_stderr, 1),
    FFMPEGFS_OPT("log_stderr",                  m_log_stderr, 1),
    FFMPEGFS_OPT("--log_syslog",                m_log_syslog, 1),
    FFMPEGFS_OPT("log_syslog",                  m_log_syslog, 1),
    FFMPEGFS_OPT("--logfile=%s",                m_logfile, 0),
    FFMPEGFS_OPT("logfile=%s",                  m_logfile, 0),

    FUSE_OPT_KEY("-h",                          KEY_HELP),
    FUSE_OPT_KEY("--help",                      KEY_HELP),
    FUSE_OPT_KEY("-V",                          KEY_VERSION),
    FUSE_OPT_KEY("--version",                   KEY_VERSION),
    FUSE_OPT_KEY("-d",                          KEY_KEEP_OPT),
    FUSE_OPT_KEY("debug",                       KEY_KEEP_OPT),
    FUSE_OPT_END
};

static int compare(const char *value, const char *pattern);
static int get_bitrate(const char * arg, unsigned int *value);
static int get_samplerate(const char * arg, unsigned int *value);
static int get_time(const char * arg, time_t *value);
static int get_size(const char * arg, size_t *value);

static int ffmpegfs_opt_proc(void* data, const char* arg, int key, struct fuse_args *outargs);
static void print_params();
static void usage(char *name);

static void usage(char *name)
{
    printf("Usage: %s [OPTION]... IN_DIR OUT_DIR\n\n", name);
    fputs("Mount IN_DIR on OUT_DIR, converting audio/video files to MP3/MP4 upon access.\n"
          "\n"
          "Encoding options:\n"
          "\n"
          "    --desttype=TYPE, -o desttype=TYPE\n"
          "                           Select destination format. Can currently be\n"
          "                           either mp3 or mp4. To stream videos, mp4 must be\n"
          "                           selected.\n"
          "                           Default: mp4\n"
      #ifndef DISABLE_ISMV
          "    --enable_ismv=0|1, -o enable_ismv=0|1\n"
          "                           Set to 1 to create a ISMV (Smooth Streaming) file.\n"
          "                           Must be used together with desttype=mp4.\n"
          "                           Resulting files will stream to Internet Explorer, but\n"
          "                           are not compatible with most other players.\n"
          "                           Default: 0\n"
      #endif
          "\n"
          "Audio Options:\n"
          "\n"
          "    --audiobitrate=BITRATE, -o audiobitrate=BITRATE\n"
          "                           Audio encoding bitrate (in kbit): Acceptable values for BITRATE\n"
          "                           include 96, 112, 128, 160, 192, 224, 256, and\n"
          "                           320.\n"
          "                           Default: 128 kbit\n"
          "    --audiosamplerate=SAMPLERATE, -o audiosamplerate=SAMPLERATE\n"
          "                           Limits the output sample rate to SAMPLERATE. If the source file\n"
          "                           sample rate is more it will be downsampled automatically.\n"
          "                           Typical values are 8000, 11025, 22050, 44100,\n"
          "                           48000, 96000, 192000. Set to 0 to keep source rate.\n"
          "                           Default: 44.1 kHz\n"
          "\n"
          "BITRATE can be defined...\n"
          "In bit/s:  #  or #bps\n"
          "In kbit/s: #M or #Mbps\n"
          "In Mbit/s: #M or #Mbps\n"
          "\n"
          "SAMPLERATE can be defined...n"
          "In Hz:  #  or #Hzn"
          "In kHz: #K or #KHzn"
          "\n"
          "Video Options:\n"
          "\n"
          "    --videobitrate=BITRATE, -o videobitrate=BITRATE\n"
          "                           Video encoding bit rate (in kbit). Acceptable values for BITRATE\n"
          "                           range between 500 and 250000. Setting this too high or low may.\n"
          "                           cause transcoding to fail.\n"
          "                           Default: 2 Mbit\n"
      #ifndef DISABLE_AVFILTER
          "    --videoheight=HEIGHT, -o videoheight=HEIGHT\n"
          "                           Sets the height of the target video.\n"
          "                           When the video is rescaled the aspect ratio is\n"
          "                           preserved if --width is not set at the same time.\n"
          "                           Default: keep source video height\n"
          "    --videowidth=WIDTH, -o videowidth=WIDTH\n"
          "                           Sets the width of the target video.\n"
          "                           Whene the video is rescaled the aspect ratio is\n"
          "                           preserved if --height is not set at the same time.\n"
          "                           Default: keep source video width\n"
          "    --deinterlace, -o deinterlace\n"
          "                           Deinterlace video if necessary while transcoding.\n"
          "                           May need higher bit rate, but will increase picture qualitiy\n"
          "                           when streaming via HTML5.\n"
          "                           Default: no deinterlace\n"
      #endif
          "\n"
          "BITRATE can be defined...\n"
          "In bit/s:  #  or #bps\n"
          "In kbit/s: #M or #Mbps\n"
          "In Mbit/s: #M or #Mbps\n"
          "\n"
          "Cache Options:\n"
          "\n"
          "     --expiry_time=TIME, -o expiry_time=TIME\n"
          "                           Cache entries expire after TIME and will be deleted\n"
          "                           to save disk space.\n"
          "                           Default: 1 week\n"
          "     --max_inactive_suspend=TIME, -o max_inactive_suspend=TIME\n"
          "                           While being accessed the file is transcoded to the target format\n"
          "                           in the background. When the client quits transcoding will continue\n"
          "                           until this time out. Transcoding is suspended until it is\n"
          "                           accessed again, and transcoding will continue.\n"
          "                           Default: 2 minutes\n"
          "     --max_inactive_abort=TIME, -o max_inactive_abort=TIME\n"
          "                           While being accessed the file is transcoded to the target format\n"
          "                           in the background. When the client quits transcoding will continue\n"
          "                           until this time out, and the transcoder thread quits\n"
          "                           Default: 5 minutes\n"
          "     --max_cache_size=SIZE, -o max_cache_size=SIZE\n"
          "                           Set the maximum diskspace used by the cache. If the cache would grow\n"
          "                           beyond this limit when a file is transcoded, old entries will be deleted\n"
          "                           to keep the cache within the size limit.\n"
          "                           Default: unlimited\n"
          "     --min_diskspace=SIZE, -o min_diskspace=SIZE\n"
          "                           Set the required diskspace on the cachepath mount. If the remaining\n"
          "                           space would fall below SIZE when a file is transcoded, old entries will\n"
          "                           be deleted to keep the diskspace within the limit.\n"
          "                           Default: 0 (no minimum space)\n"
          "     --cachepath=DIR, -o cachepath=DIR\n"
          "                           Sets the disk cache directory to DIR. Will be created if not existing.\n"
          "                           The user running ffmpegfs must have write access to the location.\n"
          "                           Default: temp directory, e.g. /tmp\n"
          "     --disable_cache, -o disable_cache\n"
          "                           Disable the cache functionality.\n"
          "                           Default: enabled\n"
          "     --prune_cache\n"
          "                           Prune cache immediately according to the above setting.\n"
          "\n"
          "TIME can be defined...\n"
          "Seconds: #\n"
          "Minutes: #m\n"
          "Hours:   #h\n"
          "Days:    #d\n"
          "Weeks:   #w\n"
          "\n"
          "SIZE can be defined...\n"
          "In bytes:  # or #B\n"
          "In KBytes: #K or #KB\n"
          "In MBytes: #B or #MB\n"
          "In GBytes: #G or #GB\n"
          "In TBytes: #T or #TB\n"
      #ifndef DISABLE_MAX_THREADS
          "\n"
          "Other:\n"
          "\n"
          "     --max_threads=COUNT, -o max_threads=COUNT\n"
          "                           Limit concurrent transcoder threads. Set to 0 for unlimited threads."
          "                           Reasonable values are up to 4 times number of CPU cores."
          "                           Default: 4 times number of detected cpu cores\n"
      #endif
          "\n"
          "Logging:\n"
          "\n"
          "    --log_maxlevel=LEVEL, -o log_maxlevel=LEVEL\n"
          "                           Maximum level of messages to log, either ERROR, WARNING, INFO, DEBUG\n"
          "                           or TRACE. Defaults to WARNING, and always set to DEBUG in debug mode.\n"
          "                           Note that the other log flags must also be set to enable logging.\n"
          "    --log_stderr, -o log_stderr\n"
          "                           Enable outputting logging messages to stderr.\n"
          "                           Enabled in debug mode.\n"
          "    --log_syslog, -o log_syslog\n"
          "                           Enable outputting logging messages to syslog.\n"
          "    --logfile=FILE, -o logfile=FILE\n"
          "                           File to output log messages to. By default, no\n"
          "                           file will be written.\n"
          "\n"
          "General/FUSE options:\n"
          "    -h, --help             display this help and exit\n"
          "    -V, --version          output version information and exit\n"
          "\n", stdout);
}

static int compare(const char *value, const char *pattern)
{
    regex_t regex;
    int reti;

    reti = regcomp(&regex, pattern, REG_EXTENDED | REG_ICASE);
    if (reti)
    {
        fprintf(stderr, "Could not compile regex\n");
        return -1;
    }

    reti = regexec(&regex, value, 0, NULL, 0);

    regfree(&regex);

    return reti;
}

// Get bitrate:
// In bit/s:  #  or #bps
// In kbit/s: #M or #Mbps
// In Mbit/s: #M or #Mbps
static int get_bitrate(const char * arg, unsigned int *value)
{
    const char * ptr = strchr(arg, '=');

    if (ptr)
    {
        int reti;

        ptr++;

        // Check for decimal number
        reti = compare(ptr, "^([1-9][0-9]*|0)?(bps)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)atol(ptr);
            return 0;   // OK
        }

        // Check for number with optional descimal point and K modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?K(bps)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)(atof(ptr) * 1000);
            return 0;   // OK
        }

        // Check for number with optional descimal point and M modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?M(bps)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)(atof(ptr) * 1000000);
            return 0;   // OK
        }

        fprintf(stderr, "Invalid bit rate '%s'\n", ptr);
    }
    else
    {
        fprintf(stderr, "Invalid bit rate\n");
    }

    return -1;
}

// Get sample rate:
// In Hz:  #  or #Hz
// In kHz: #K or #KHz
static int get_samplerate(const char * arg, unsigned int * value)
{
    const char * ptr = strchr(arg, '=');

    if (ptr)
    {
        int reti;

        ptr++;

        // Check for decimal number
        reti = compare(ptr, "^([1-9][0-9]*|0)(Hz)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)atol(ptr);
            return 0;   // OK
        }

        // Check for number with optional descimal point and K modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?K(Hz)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)(atof(ptr) * 1000);
            return 0;   // OK
        }

        fprintf(stderr, "Invalid sample rate '%s'\n", ptr);
    }
    else
    {
        fprintf(stderr, "Invalid sample rate\n");
    }

    return -1;
}

// Get time in format
// Seconds: #
// Minutes: #m
// Hours:   #h
// Days:    #d
// Weeks:   #w
static int get_time(const char * arg, time_t *value)
{
    const char * ptr = strchr(arg, '=');

    if (ptr)
    {
        int reti;

        ptr++;

        // Check for decimal number
        reti = compare(ptr, "^([1-9][0-9]*|0)?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (unsigned int)atol(ptr);
            return 0;   // OK
        }

        // Check for number with optional descimal point and m modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?m$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (time_t)atol(ptr) * 60;
            return 0;   // OK
        }

        // Check for number with optional descimal point and h modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?h$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (time_t)atol(ptr) * 60 * 60;
            return 0;   // OK
        }

        // Check for number with optional descimal point and d modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?d$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (time_t)atol(ptr) * 60 * 60 * 24;
            return 0;   // OK
        }

        // Check for number with optional descimal point and w modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?w$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (time_t)atol(ptr) * 60 * 60 * 24 * 7;
            return 0;   // OK
        }

        fprintf(stderr, "Invalid time format '%s'\n", ptr);
    }
    else
    {
        fprintf(stderr, "Invalid time format\n");
    }

    return -1;
}

// Read size:
// In bytes:  # or #B
// In KBytes: #K or #KB
// In MBytes: #B or #MB
// In GBytes: #G or #GB
// In TBytes: #T or #TB
static int get_size(const char * arg, size_t *value)
{
    const char * ptr = strchr(arg, '=');

    if (ptr)
    {
        int reti;

        ptr++;

        // Check for decimal number
        reti = compare(ptr, "^([1-9][0-9]*|0)?B?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (size_t)atol(ptr);
            return 0;   // OK
        }

        // Check for number with optional descimal point and K/KB modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?KB?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (size_t)(atof(ptr) * 1024);
            return 0;   // OK
        }

        // Check for number with optional descimal point and M/MB modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?MB?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (size_t)(atof(ptr) * 1024 * 1024);
            return 0;   // OK
        }

        // Check for number with optional descimal point and G/GB modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?GB?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (size_t)(atof(ptr) * 1024 * 1024 * 1024);
            return 0;   // OK
        }

        // Check for number with optional descimal point and T/TB modifier
        reti = compare(ptr, "^[1-9][0-9]*(\\.[0-9]+)?TB?$");

        if (reti == -1)
        {
            return -1;
        }
        else if (!reti)
        {
            *value = (size_t)(atof(ptr) * 1024 * 1024 * 1024 * 1024);
            return 0;   // OK
        }

        fprintf(stderr, "Invalid size '%s'\n", ptr);
    }
    else
    {
        fprintf(stderr, "Invalid size\n");
    }

    return -1;
}

static int ffmpegfs_opt_proc(void* data, const char* arg, int key, struct fuse_args *outargs)
{
    static int n;
    (void)data;

    switch (key)
    {
    case FUSE_OPT_KEY_NONOPT:
    {
        // check for basepath and bitrate parameters
        if (n == 0 && !params.m_basepath)
        {
            params.m_basepath = arg;
            n++;
            return 0;
        }
        else if (n == 1 && !params.m_mountpath)
        {
            params.m_mountpath = arg;
            n++;
            return 1;
        }

        break;
    }
    case KEY_HELP:
    {
        usage(outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &ffmpegfs_ops, NULL);
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
        fuse_main(outargs->argc, outargs->argv, &ffmpegfs_ops, NULL);
        exit(0);
    }
    case KEY_AUDIO_BITRATE:
    {
        return get_bitrate(arg, &params.m_audiobitrate);
    }
    case KEY_AUDIO_SAMPLERATE:
    {
        return get_samplerate(arg, &params.m_audiosamplerate);
    }
    case KEY_VIDEO_BITRATE:
    {
        return get_bitrate(arg, &params.m_videobitrate);
    }
    case KEY_EXPIRY_TIME:
    {
        return get_time(arg, &params.m_expiry_time);
    }
    case KEY_MAX_INACTIVE_SUSPEND_TIME:
    {
        return get_time(arg, &params.m_max_inactive_suspend);
    }
    case KEY_MAX_INACTIVE_ABORT_TIME:
    {
        return get_time(arg, &params.m_max_inactive_abort);
    }
    case KEY_MAX_CACHE_SIZE:
    {
        return get_size(arg, &params.m_max_cache_size);
    }
    case KEY_MIN_DISKSPACE_SIZE:
    {
        return get_size(arg, &params.m_min_diskspace);
    }
    }

    return 1;
}

static void print_params()
{
    char cachepath[PATH_MAX];
    enum AVCodecID audio_codecid = AV_CODEC_ID_NONE;
    enum AVCodecID video_codecid = AV_CODEC_ID_NONE;
    char audiobitrate[100];
    char audiosamplerate[100];
    char videobitrate[100];
    char expiry_time[100];
    char max_inactive_suspend[100];
    char max_inactive_abort[100];
    char max_cache_size[100];
    char min_diskspace[100];

    cache_path(cachepath, sizeof(cachepath));

#ifndef DISABLE_ISMV
    get_codecs(params.m_desttype, NULL, &audio_codecid, &video_codecid, params.m_enable_ismv);
#else
    get_codecs(params.m_desttype, NULL, &audio_codecid, &video_codecid, 0);
#endif

    format_bitrate(audiobitrate, sizeof(audiobitrate), params.m_audiobitrate);
    format_samplerate(audiosamplerate, sizeof(audiosamplerate), params.m_audiosamplerate);
    format_bitrate(videobitrate, sizeof(videobitrate), params.m_videobitrate);
    format_time(expiry_time, sizeof(expiry_time), params.m_expiry_time);
    format_time(max_inactive_suspend, sizeof(expiry_time), params.m_max_inactive_suspend);
    format_time(max_inactive_abort, sizeof(max_inactive_abort), params.m_max_inactive_abort);
    format_size(max_cache_size, sizeof(max_cache_size), params.m_max_cache_size);
    format_size(min_diskspace, sizeof(min_diskspace), params.m_min_diskspace);

    ffmpegfs_info(PACKAGE_NAME " options:\n\n"
                               "Base Path         : %s\n"
                               "Mount Path        : %s\n\n"
                               "Destination Type  : %s\n"
              #ifndef DISABLE_ISMV
                               "Use ISMV          : %s\n"
              #endif
                               "Audio Format      : %s\n"
                               "Audio Bitrate     : %s\n"
                               "Audio Sample Rate : %s\n"
              #ifndef DISABLE_AVFILTER
                               "Video Size        : %ux%u pixels\n"
                               "Deinterlace       : %s\n"
              #endif
                               "Video Format      : %s\n"
                               "Video Bitrate     : %s\n"
                               "Max. Log Level    : %s\n"
                               "Log to stderr     : %u\n"
                               "Log to syslog     : %u\n"
                               "Logfile           : %s\n"
                               "\nCache Settings\n\n"
                               "Expiry Time       : %s\n"
                               "Inactivity Suspend: %s\n"
                               "Inactivity Abort  : %s\n"
                               "Max. Cache Size   : %s\n"
                               "Min. Disk Space   : %s\n"
              #ifndef DISABLE_MAX_THREADS
                               "Max. Threads      : %u\n"
              #endif
                               "Cache Path        : %s\n",
                  params.m_basepath,
                  params.m_mountpath,
                  params.m_desttype,
              #ifndef DISABLE_ISMV
                  params.m_enable_ismv ? "yes" : "no",
              #endif
                  get_codec_name(audio_codecid),
                  audiobitrate,
                  audiosamplerate,
              #ifndef DISABLE_AVFILTER
                  params.m_videowidth,
                  params.m_videoheight,
                  params.m_deinterlace ? "yes" : "no",
              #endif
                  get_codec_name(video_codecid),
                  videobitrate,
                  params.m_log_maxlevel,
                  params.m_log_stderr,
                  params.m_log_syslog,
                  *params.m_logfile ? params.m_logfile : "none",
                  expiry_time,
                  max_inactive_suspend,
                  max_inactive_abort,
                  max_cache_size,
                  min_diskspace,
              #ifndef DISABLE_MAX_THREADS
                  params.m_max_threads,
              #endif
                  cachepath);
}

int main(int argc, char *argv[])
{
    int ret;

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    printf("%s V%s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Copyright (C) 2006-2008 David Collett\n"
           "Copyright (C) 2008-2012 K. Henriksson\n"
           "Copyright (C) 2017 FFmpeg support by Norbert Schlia (nschlia@oblivion-software.de)\n\n");

    // Configure FFmpeg
    // register all the codecs
    avcodec_register_all();
    av_register_all();
    //show_formats_devices(0);
#ifndef USING_LIBAV
    // Redirect FFmpeg logs
    av_log_set_callback(ffmpeg_log);
#endif

#ifndef DISABLE_MAX_THREADS
    params.m_max_threads = get_nprocs() * 4;
#endif

    if (fuse_opt_parse(&args, &params, ffmpegfs_opts, ffmpegfs_opt_proc))
    {
        fprintf(stderr, "ERROR: Parsing options.\n\n");
        //usage(argv[0]);
        return 1;
    }

    // Log to the screen, and enable debug messages, if debug is enabled.
    if (params.m_debug)
    {
        params.m_log_stderr = 1;
        params.m_log_maxlevel = "DEBUG";
        // av_log_set_level(AV_LOG_DEBUG);
        av_log_set_level(AV_LOG_INFO);
    }
    else
    {
        av_log_set_level(AV_LOG_QUIET);
    }

    if (!init_logging(params.m_logfile, params.m_log_maxlevel, params.m_log_stderr, params.m_log_syslog))
    {
        fprintf(stderr, "ERROR: Failed to initialise logging module.\n");
        fprintf(stderr, "Maybe log file couldn't be opened for writing?\n\n");
        return 1;
    }

    if (params.m_prune_cache)
    {
        if (args.argc > 1)
        {
            fprintf(stderr, "ERROR: Invalid additional parameters for --prune_cache:\n");
            for (int n = 1; n < args.argc; n++)
            {
                fprintf(stderr, "Invalid: '%s'\n", args.argv[n]);
            }
            return 1;
        }

        // Prune cache and exit
        if (cache_new())
        {
            return 1;
        }
        transcoder_prune_cache();
        return 0;
    }

    if (!params.m_basepath)
    {
        fprintf(stderr, "ERROR: No valid basepath specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.m_basepath[0] != '/')
    {
        fprintf(stderr, "ERROR: basepath must be an absolute path.\n\n");
        usage(argv[0]);
        return 1;
    }

    struct stat st;
    if (stat(params.m_basepath, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "ERROR: basepath is not a valid directory: %s\n\n", params.m_basepath);
        usage(argv[0]);
        return 1;
    }

    if (!params.m_mountpath)
    {
        fprintf(stderr, "ERROR: No valid mountpath specified.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (params.m_mountpath[0] != '/')
    {
        fprintf(stderr, "ERROR: mountpath must be an absolute path.\n\n");
        usage(argv[0]);
        return 1;
    }

    if (stat(params.m_mountpath, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        fprintf(stderr, "ERROR: mountpath is not a valid directory: %s\n\n", params.m_mountpath);
        usage(argv[0]);
        return 1;
    }

    // Check for valid destination type.
    if (!check_encoder(params.m_desttype))
    {
        fprintf(stderr, "ERROR: No encoder available for desttype: %s\n\n", params.m_desttype);
        usage(argv[0]);
        return 1;
    }

    if (cache_new())
    {
        return 1;
    }

    print_params();

    // start FUSE
    ret = fuse_main(args.argc, args.argv, &ffmpegfs_ops, NULL);

    fuse_opt_free_args(&args);

    return ret;
}
