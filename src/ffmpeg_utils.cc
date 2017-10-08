/*
 * FFMPEG decoder class source for mp3fs
 *
 * Copyright (C) 2015 Thomas Schwarzenberger
 * FFMPEG supplementals by Norbert Schlia (nschlia@oblivon-software.de)
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

#include "ffmpeg_utils.h"

#include <algorithm>
#include <cstdlib>

#include <unistd.h>

#include <vector>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int is_device(const AVClass *avclass);

#ifndef AV_ERROR_MAX_STRING_SIZE
#define AV_ERROR_MAX_STRING_SIZE 128
#endif // AV_ERROR_MAX_STRING_SIZE

std::string ffmpeg_geterror(int errnum)
{
    char error[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, error, AV_ERROR_MAX_STRING_SIZE);
    return error;
}

double ffmpeg_cvttime(int64_t ts, const AVRational & time_base)
{
    if (ts != 0 && ts != (int64_t)AV_NOPTS_VALUE)
    {
        return ((double)ts * ::av_q2d(time_base));
    }
    else
    {
        return 0;
    }
}

#if !(LIBAVUTIL_VERSION_MICRO >= 100 && LIBAVUTIL_VERSION_INT >= LIBAVUTIL_MIN_VERSION_INT )
const char *get_media_type_string(enum AVMediaType media_type)
{
    switch (media_type) {
    case AVMEDIA_TYPE_VIDEO:      return "video";
    case AVMEDIA_TYPE_AUDIO:      return "audio";
    case AVMEDIA_TYPE_DATA:       return "data";
    case AVMEDIA_TYPE_SUBTITLE:   return "subtitle";
    case AVMEDIA_TYPE_ATTACHMENT: return "attachment";
    default:                      return "unknown";
    }
}
#endif

static std::string ffmpeg_libinfo(
        bool bLibExists,
        unsigned int /*nVersion*/,
        const char * /*pszCfg*/,
        int nVersionMinor,
        int nVersionMajor,
        int nVersionMicro,
        const char * pszLibname)
{
    std::string info;

    if (bLibExists)
    {
        char buffer[1024];

        sprintf(buffer,
                    "lib%-15s: %d.%d.%d\n",
                    pszLibname,
                    nVersionMinor,
                    nVersionMajor,
                    nVersionMicro);
        info = buffer;
    }

    return info;
}

void ffmpeg_libinfo(char * buffer, size_t maxsize)
{
#define PRINT_LIB_INFO(libname, LIBNAME) \
    ffmpeg_libinfo(true, libname##_version(), libname##_configuration(), \
    LIB##LIBNAME##_VERSION_MAJOR, LIB##LIBNAME##_VERSION_MINOR, LIB##LIBNAME##_VERSION_MICRO, #libname)

    std::string info;

    info = "FFMPEG Version:\n";

    info += PRINT_LIB_INFO(avutil,      AVUTIL);
    info += PRINT_LIB_INFO(avcodec,     AVCODEC);
    info += PRINT_LIB_INFO(avformat,    AVFORMAT);
//    info += PRINT_LIB_INFO(avdevice,    AVDEVICE);
//    info += PRINT_LIB_INFO(avfilter,    AVFILTER);
    info += PRINT_LIB_INFO(avresample,  AVRESAMPLE);
//    info += PRINT_LIB_INFO(swscale,     SWSCALE);
//    info += PRINT_LIB_INFO(swresample,  SWRESAMPLE);
//    info += PRINT_LIB_INFO(postproc,    POSTPROC);

    *buffer = '\0';
    strncat(buffer, info.c_str(), maxsize - 1);
}

static int is_device(const AVClass *avclass)
{
    if (!avclass)
        return 0;

    return 0;
    //return AV_IS_INPUT_DEVICE(avclass->category) || AV_IS_OUTPUT_DEVICE(avclass->category);
}

int show_formats_devices(int device_only)
{
    AVInputFormat *ifmt  = NULL;
//    AVOutputFormat *ofmt = NULL;
    const char *last_name;
    int is_dev;

    printf("%s\n"
           " D. = Demuxing supported\n"
           " .E = Muxing supported\n"
           " --\n", device_only ? "Devices:" : "File formats:");
    last_name = "000";
    for (;;) {
        int decode = 0;
        int encode = 0;
        const char *name      = NULL;
        const char *long_name = NULL;
        const char *extensions = NULL;

//        while ((ofmt = av_oformat_next(ofmt))) {
//            is_dev = is_device(ofmt->priv_class);
//            if (!is_dev && device_only)
//                continue;
//            if ((!name || strcmp(ofmt->name, name) < 0) &&
//                    strcmp(ofmt->name, last_name) > 0) {
//                name      = ofmt->name;
//                long_name = ofmt->long_name;
//                encode    = 1;
//            }
//        }
        while ((ifmt = av_iformat_next(ifmt))) {
            is_dev = is_device(ifmt->priv_class);
            if (!is_dev && device_only)
                continue;
            if ((!name || strcmp(ifmt->name, name) < 0) &&
                    strcmp(ifmt->name, last_name) > 0) {
                name      = ifmt->name;
                long_name = ifmt->long_name;
                extensions = ifmt->extensions;
                encode    = 0;
            }
            if (name && strcmp(ifmt->name, name) == 0)
                decode = 1;
        }
        if (!name)
            break;
        last_name = name;
        if (!extensions)
            continue;

        printf(" %s%s %-15s %-15s %s\n",
               decode ? "D" : " ",
               encode ? "E" : " ",
               extensions != NULL ? extensions : "-",
               name,
               long_name ? long_name:" ");
    }
    return 0;
}

#pragma GCC diagnostic pop
