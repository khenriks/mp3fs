/*
 * FFmpeg decoder class header for ffmpegfs
 *
 * Copyright (C) 2017 Norbert Schlia (nschlia@oblivion-software.de)
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

#ifndef FFMPEG_UTILS_H
#define FFMPEG_UTILS_H

#pragma once

// Force PRId64 defines
#define __STDC_FORMAT_MACROS

// Disable annoying warnings outside our code
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
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavresample/avresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/audio_fifo.h>
#include "libavutil/avstring.h"
#if LIBAVUTIL_VERSION_MICRO >= 100
// Does not exist in Libav
#include "libavutil/ffversion.h"
#endif

//#include "libavdevice/avdevice.h"
//#include <libavformat/avio.h>
//#include <libavutil/audio_fifo.h>
//#include <libavutil/avstring.h>
//#include <libavutil/channel_layout.h>
//#include <libavutil/common.h>
//#include <libavutil/error.h>
//#include <libavutil/frame.h>
//#include <libavutil/imgutils.h>
//#include <libavutil/mathematics.h>
//#include <libavutil/mem.h>
//#include <libavutil/samplefmt.h>
//#include <libavutil/timestamp.h>
//#include <libswscale/swscale.h>
#ifdef __cplusplus
}
#endif
#pragma GCC diagnostic pop

#ifndef LIBAVUTIL_VERSION_MICRO
#error "LIBAVUTIL_VERSION_MICRO not defined. Missing include header?"
#endif

// Libav detection:
// FFmpeg has library micro version >= 100
// Libav  has library micro version < 100
// So if micro < 100, it's Libav, else it's FFmpeg.
#if LIBAVUTIL_VERSION_MICRO < 100
#define USING_LIBAV
#endif

// Minumum version required for special functionality
// Please note that I am not 100% sure from which FFmpeg version on this works, so lower
// versions may also work.
// Also as this was not 100% tested with Libav this only applies to FFmpeg. With Libav
// none of the extensions will be used.
#define LIBAVCODEC_MIN_VERSION_INT      AV_VERSION_INT( 57, 64, 101 )
#define LIBAVFORMAT_MIN_VERSION_INT     AV_VERSION_INT( 57, 25, 100 )
#define LIBAVUTIL_MIN_VERSION_INT       AV_VERSION_INT( 55, 34, 101 )

#if (LIBAVUTIL_VERSION_MICRO >= 100 && LIBAVUTIL_VERSION_INT >= LIBAVUTIL_MIN_VERSION_INT )
#define get_media_type_string av_get_media_type_string
#else
const char *get_media_type_string(enum AVMediaType media_type);
#endif

// Ignore if this is missing
#ifndef AV_ROUND_PASS_MINMAX
#define AV_ROUND_PASS_MINMAX            0
#endif

// These once had a different name
#if !defined(AV_CODEC_CAP_DELAY) && defined(CODEC_CAP_DELAY)
#define AV_CODEC_CAP_DELAY              CODEC_CAP_DELAY
#endif

#if !defined(AV_CODEC_CAP_TRUNCATED) && defined(CODEC_CAP_TRUNCATED)
#define AV_CODEC_CAP_TRUNCATED          CODEC_CAP_TRUNCATED
#endif

#if !defined(AV_CODEC_FLAG_TRUNCATED) && defined(CODEC_FLAG_TRUNCATED)
#define AV_CODEC_FLAG_TRUNCATED         CODEC_FLAG_TRUNCATED
#endif

#ifndef AV_CODEC_FLAG_GLOBAL_HEADER
#define AV_CODEC_FLAG_GLOBAL_HEADER     CODEC_FLAG_GLOBAL_HEADER;
#endif

#ifndef FF_INPUT_BUFFER_PADDING_SIZE
#define FF_INPUT_BUFFER_PADDING_SIZE    256
#endif

typedef enum _tagOUTPUTTYPE
{
    TYPE_UNKNOWN,
    TYPE_MP3,
    TYPE_MP4
} OUTPUTTYPE;

#ifdef __cplusplus
#include <string>
#include <libavutil/rational.h>

using namespace std;

string ffmpeg_geterror(int errnum);
double ffmpeg_cvttime(int64_t ts, const AVRational & time_base);
#endif

#ifdef __cplusplus
extern "C" {
#endif
void ffmpeg_libinfo(char * buffer, size_t maxsize);
int show_formats_devices(int device_only);
const char * get_codec_name(enum AVCodecID codec_id);
const char * get_codecs(const char * type, OUTPUTTYPE * output_type, enum AVCodecID * audio_codecid, enum AVCodecID * video_codecid, int m_enable_ismv);

void format_bitrate(char *output, size_t size, unsigned int value);
void format_samplerate(char *output, size_t size, unsigned int value);
void format_time(char *output, size_t size, time_t value);
void format_size(char *output, size_t size, size_t value);

#ifdef __cplusplus
}
#endif

int mktree(const char *path, mode_t mode);
void tempdir(char *dir, size_t size);

#ifdef USING_LIBAV
// Libav does not have these functions
int avformat_alloc_output_context2(AVFormatContext **avctx, AVOutputFormat *oformat, const char *format, const char *filename);
const char *avcodec_get_name(enum AVCodecID id);
#endif

#endif
