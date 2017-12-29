/*
 * FFmpeg utilities for ffmpegfs
 *
 * Copyright (C) 2017 by Norbert Schlia (nschlia@oblivion-software.de)
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
#include "id3v1tag.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int is_device(const AVClass *avclass);

#ifndef AV_ERROR_MAX_STRING_SIZE
#define AV_ERROR_MAX_STRING_SIZE 128
#endif // AV_ERROR_MAX_STRING_SIZE

string ffmpeg_geterror(int errnum)
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
    switch (media_type)
    {
    case AVMEDIA_TYPE_VIDEO:
        return "video";
    case AVMEDIA_TYPE_AUDIO:
        return "audio";
    case AVMEDIA_TYPE_DATA:
        return "data";
    case AVMEDIA_TYPE_SUBTITLE:
        return "subtitle";
    case AVMEDIA_TYPE_ATTACHMENT:
        return "attachment";
    default:
        return "unknown";
    }
}
#endif

static string ffmpeg_libinfo(
        bool bLibExists,
        unsigned int /*nVersion*/,
        const char * /*pszCfg*/,
        int nVersionMinor,
        int nVersionMajor,
        int nVersionMicro,
        const char * pszLibname)
{
    string info;

    if (bLibExists)
    {
        char buffer[1024];

        sprintf(buffer,
                "lib%-17s: %d.%d.%d\n",
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

    string info;

#ifdef USING_LIBAV
    info = "Libav Version       :\n";
#else
    info = "FFmpeg Version      : " FFMPEG_VERSION "\n";
#endif

    info += PRINT_LIB_INFO(avutil,      AVUTIL);
    info += PRINT_LIB_INFO(avcodec,     AVCODEC);
    info += PRINT_LIB_INFO(avformat,    AVFORMAT);
    // info += PRINT_LIB_INFO(avdevice,    AVDEVICE);
    // info += PRINT_LIB_INFO(avfilter,    AVFILTER);
    // info += PRINT_LIB_INFO(swresample,  SWRESAMPLE);
    info += PRINT_LIB_INFO(avresample,  AVRESAMPLE);
    info += PRINT_LIB_INFO(swscale,     SWSCALE);
    // info += PRINT_LIB_INFO(postproc,    POSTPROC);

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
    for (;;)
    {
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
        while ((ifmt = av_iformat_next(ifmt)))
        {
            is_dev = is_device(ifmt->priv_class);
            if (!is_dev && device_only)
                continue;
            if ((!name || strcmp(ifmt->name, name) < 0) &&
                    strcmp(ifmt->name, last_name) > 0)
            {
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

const char * get_codec_name(enum AVCodecID codec_id)
{
    const AVCodecDescriptor * pCodecDescriptor;
    const char * psz = "";

    pCodecDescriptor = avcodec_descriptor_get(codec_id);

    if (pCodecDescriptor != NULL)
    {
        if (pCodecDescriptor->long_name != NULL)
        {
            psz = pCodecDescriptor->long_name;
        }

        else
        {
            psz = pCodecDescriptor->name;
        }
    }

    return psz;
}

#pragma GCC diagnostic pop

int mktree(const char *path, mode_t mode)
{
    char *_path = strdup(path);

    if (_path == NULL)
    {
        return ENOMEM;
    }

    char dir[PATH_MAX] = "\0";
    char *p = strtok (_path, "/");
    int status = 0;

    while (p != NULL)
    {
        int newstat;

        strcat(dir, "/");
        strcat(dir, p);

        errno = 0;

        newstat = mkdir(dir, mode);

        if (!status && newstat && errno != EEXIST)
        {
            status = -1;
            break;
        }

        status = newstat;

        p = strtok (NULL, "/");
    }

    free(_path);
    return status;
}

void tempdir(char *dir, size_t size)
{
    *dir = '\0';
    const char *temp = getenv("TMPDIR");

    if (temp != NULL)
    {
        strncpy(dir, temp, size);
        return;
    }

    strncpy(dir, P_tmpdir, size);

    if (*dir)
    {
        return;
    }

    strncpy(dir, "/tmp", size);
}

const char * get_codecs(const char * type, OUTPUTTYPE * output_type, AVCodecID * audio_codecid, AVCodecID * video_codecid, int m_enable_ismv)
{
    if (!strcasecmp(type, "mp3"))
    {
        *audio_codecid = AV_CODEC_ID_MP3;
        *video_codecid = AV_CODEC_ID_NONE; //AV_CODEC_ID_MJPEG;
        if (output_type != NULL)
        {
            *output_type = TYPE_MP3;
        }
        return "mp3";
    }
    else if (!strcasecmp(type, "mp4"))
    {
        *audio_codecid = AV_CODEC_ID_AAC;
        *video_codecid = AV_CODEC_ID_H264;
        if (output_type != NULL)
        {
            *output_type = TYPE_MP4;
        }
        return m_enable_ismv ? "ismv" : "mp4";
    }
    else
    {
        return NULL;
    }
}

#ifdef USING_LIBAV
int avformat_alloc_output_context2(AVFormatContext **avctx, AVOutputFormat *oformat, const char *format, const char *filename)
{
    AVFormatContext *s = avformat_alloc_context();
    int ret = 0;

    *avctx = NULL;
    if (!s)
        goto nomem;

    if (!oformat)
    {
        if (format)
        {
            oformat = av_guess_format(format, NULL, NULL);
            if (!oformat)
            {
                av_log(s, AV_LOG_ERROR, "Requested output format '%s' is not a suitable output format\n", format);
                ret = AVERROR(EINVAL);
                goto error;
            }
        }
        else
        {
            oformat = av_guess_format(NULL, filename, NULL);
            if (!oformat)
            {
                ret = AVERROR(EINVAL);
                av_log(s, AV_LOG_ERROR, "Unable to find a suitable output format for '%s'\n",
                       filename);
                goto error;
            }
        }
    }

    s->oformat = oformat;
    if (s->oformat->priv_data_size > 0)
    {
        s->priv_data = av_mallocz(s->oformat->priv_data_size);
        if (!s->priv_data)
            goto nomem;
        if (s->oformat->priv_class)
        {
            *(const AVClass**)s->priv_data= s->oformat->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    }
    else
        s->priv_data = NULL;

    if (filename)
        av_strlcpy(s->filename, filename, sizeof(s->filename));
    *avctx = s;
    return 0;
nomem:
    av_log(s, AV_LOG_ERROR, "Out of memory\n");
    ret = AVERROR(ENOMEM);
error:
    avformat_free_context(s);
    return ret;
}

const char *avcodec_get_name(enum AVCodecID id)
{
    const AVCodecDescriptor *cd;
    AVCodec *codec;

    if (id == AV_CODEC_ID_NONE)
        return "none";
    cd = avcodec_descriptor_get(id);
    if (cd)
        return cd->name;
    av_log(NULL, AV_LOG_WARNING, "Codec 0x%x is not in the full list.\n", id);
    codec = avcodec_find_decoder(id);
    if (codec)
        return codec->name;
    codec = avcodec_find_encoder(id);
    if (codec)
        return codec->name;
    return "unknown_codec";
}
#endif

void init_id3v1(ID3v1 *id3v1)
{
    // Initialise ID3v1.1 tag structure
    memset(id3v1, ' ', sizeof(ID3v1));
    memcpy(&id3v1->m_sTAG, "TAG", 3);
    id3v1->m_bPad = '\0';
    id3v1->m_bTitleNo = 0;
    id3v1->m_bGenre = 0;
}

void format_number(char *output, size_t size, unsigned int value)
{
    if (!value)
    {
        strncpy(output, "unlimited", size);
        return;
    }

    snprintf(output, size, "%u", value);
}

void format_bitrate(char *output, size_t size, unsigned int value)
{
    if (value > 1000000)
    {
        snprintf(output, size, "%.2f Mbps", (double)value / 1000000);
    }
    else if (value > 1000)
    {
        snprintf(output, size, "%.1f kbps", (double)value / 1000);
    }
    else
    {
        snprintf(output, size, "%u bps", value);
    }
}

void format_samplerate(char *output, size_t size, unsigned int value)
{
    if (value < 1000)
    {
        snprintf(output, size, "%u Hz", value);
    }
    else
    {
        snprintf(output, size, "%.3f kHz", (double)value / 1000);
    }
}

void format_time(char *output, size_t size, time_t value)
{
    if (!value)
    {
        strncpy(output, "unlimited", size);
        return;
    }

    int weeks;
    int days;
    int hours;
    int mins;
    int secs;
    int pos;

    weeks = (int)(value / (60*60*24*7));
    value -= weeks * (60*60*24*7);
    days = (int)(value / (60*60*24));
    value -= days * (60*60*24);
    hours = (int)(value / (60*60));
    value -= hours * (60*60);
    mins = (int)(value / (60));
    value -= mins * (60);
    secs = (int)(value);

    *output = '0';
    pos = 0;
    if (weeks)
    {
        pos += snprintf(output, size, "%iw ", weeks);
    }
    if (days)
    {
        pos += snprintf(output + pos, size - pos, "%id ", days);
    }
    if (hours)
    {
        pos += snprintf(output + pos, size - pos, "%ih ", hours);
    }
    if (mins)
    {
        pos += snprintf(output + pos, size - pos, "%im ", mins);
    }
    if (secs)
    {
        pos += snprintf(output + pos, size - pos, "%is ", secs);
    }

}

void format_size(char *output, size_t size, size_t value)
{
    if (!value)
    {
        strncpy(output, "unlimited", size);
        return;
    }

    if (value > 1024*1024*1024*1024LL)
    {
        snprintf(output, size, "%.3f TB", (double)value / (1024*1024*1024*1024LL));
    }
    else if (value > 1024*1024*1024)
    {
        snprintf(output, size, "%.2f GB", (double)value / (1024*1024*1024));
    }
    else if (value > 1024*1024)
    {
        snprintf(output, size, "%.1f MB", (double)value / (1024*1024));
    }
    else if (value > 1024)
    {
        snprintf(output, size, "%.1f KB", (double)value / (1024));
    }
    else
    {
        snprintf(output, size, "%zu bytes", value);
    }
}

string format_number(unsigned int value)
{
    char buffer[100];

    format_number(buffer, sizeof(buffer), value);

    return buffer;
}

string format_bitrate(unsigned int value)
{
    char buffer[100];

    format_bitrate(buffer, sizeof(buffer), value);

    return buffer;
}

string format_samplerate(unsigned int value)
{
    char buffer[100];

    format_samplerate(buffer, sizeof(buffer), value);

    return buffer;
}

string format_time(time_t value)
{
    char buffer[100];

    format_time(buffer, sizeof(buffer), value);

    return buffer;
}

string format_size(size_t value)
{
    char buffer[100];

    format_size(buffer, sizeof(buffer), value);

    return buffer;
}
