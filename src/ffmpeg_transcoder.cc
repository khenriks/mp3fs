/*
 * FFmpeg decoder class source for mp3fs
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

#include "ffmpeg_transcoder.h"

#include <algorithm>
#include <cstdlib>

#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "transcode.h"
#include "buffer.h"

#define INVALID_STREAM  -1

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
FFMPEG_Transcoder::FFMPEG_Transcoder()
    : m_nCalculated_size(0)
    , m_bIsVideo(false)
    , m_pAudio_resample_ctx(NULL)
    , m_pAudioFifo(NULL)
    , m_pSws_ctx(NULL)
    , m_pts(AV_NOPTS_VALUE)
    , m_pos(AV_NOPTS_VALUE)
    , m_in({
           .m_pFormat_ctx = NULL,
           .m_pAudio_codec_ctx = NULL,
           .m_pVideo_codec_ctx = NULL,
           .m_pAudio_stream = NULL,
           .m_pVideo_stream = NULL,
           .m_nAudio_stream_idx = INVALID_STREAM,
           .m_nVideo_stream_idx = INVALID_STREAM
        })
    , m_out({
            .m_output_type = TYPE_UNKNOWN,
            .m_pFormat_ctx = NULL,
            .m_pAudio_codec_ctx = NULL,
            .m_pVideo_codec_ctx = NULL,
            .m_pAudio_stream = NULL,
            .m_pVideo_stream = NULL,
            .m_nAudio_stream_idx = INVALID_STREAM,
            .m_nVideo_stream_idx = INVALID_STREAM,
            .m_nAudio_pts = 0,
            .m_audio_start_pts = 0,
            .m_video_start_pts = 0,
            .m_last_mux_dts = 0,
            })
{
#pragma GCC diagnostic pop
    mp3fs_debug("FFmpeg trancoder: ready to initialise.");

    // Initialise ID3v1.1 tag structure
    memset(&m_out.m_id3v1, ' ', sizeof(ID3v1));
    memcpy(&m_out.m_id3v1.m_sTAG, "TAG", 3);
    m_out.m_id3v1.m_bPad = '\0';
    m_out.m_id3v1.m_bTitleNo = 0;
    m_out.m_id3v1.m_bGenre = 0;
}

/* Free the FFmpeg en/decoder
 * after the transcoding process has finished.
 */
FFMPEG_Transcoder::~FFMPEG_Transcoder() {

    // Close fifo and resample context
    close();

    mp3fs_debug("FFmpeg trancoder: object destroyed.");
}

/*
 * Open codec context for desired media type
 */

int FFMPEG_Transcoder::open_codec_context(int *stream_idx, AVCodecContext **avctx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename)
{
    int ret;

    ret = av_find_best_stream(fmt_ctx, type, INVALID_STREAM, INVALID_STREAM, NULL, 0);
    if (ret < 0) {
        if (ret != AVERROR_STREAM_NOT_FOUND)    // Not an error
        {
            mp3fs_error("Could not find %s stream in input file '%s' (error '%s').", get_media_type_string(type), filename, ffmpeg_geterror(ret).c_str());
        }
        return ret;
    } else {
        int stream_index;
        AVCodecContext *dec_ctx = NULL;
        AVCodec *dec = NULL;
        AVDictionary *opts = NULL;
        AVStream *in_stream;
        AVCodecID codec_id = AV_CODEC_ID_NONE;

        stream_index = ret;
        in_stream = fmt_ctx->streams[stream_index];

        /* Init the decoders, with or without reference counting */
        // av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);

#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )
        /** allocate a new decoding context */
        dec_ctx = avcodec_alloc_context3(dec);
        if (!dec_ctx) {
            mp3fs_error("Could not allocate a decoding context.");
            return AVERROR(ENOMEM);
        }

        /** initialise the stream parameters with demuxer information */
        ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
        if (ret < 0) {
            return ret;
        }

        codec_id = in_stream->codecpar->codec_id;
#else
        dec_ctx = in_stream->codec;

        codec_id = dec_ctx->codec_id;
#endif

        /** Find a decoder for the stream. */
        dec = avcodec_find_decoder(codec_id);
        if (!dec) {
            mp3fs_error("Failed to find %s codec.", get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        dec_ctx->codec_id = dec->id;

        ret = avcodec_open2(dec_ctx, dec, &opts);

        av_dict_free(&opts);

        if (ret < 0) {
            mp3fs_error("Failed to find %s input codec (error '%s').", get_media_type_string(type), ffmpeg_geterror(ret).c_str());
            return ret;
        }

        mp3fs_debug("Successfully opened %s input codec.", get_codec_name(codec_id));

        *stream_idx = stream_index;

        *avctx = dec_ctx;
    }

    return 0;
}

/*
 * FFmpeg handles cover arts like video streams.
 * Try to find out if we have a video stream or a cover art.
 */
bool FFMPEG_Transcoder::is_video() const
{
    bool bIsVideo = false;

    if (m_in.m_pVideo_codec_ctx != NULL && m_in.m_pVideo_stream != NULL)
    {
        if ((m_in.m_pVideo_codec_ctx->codec_id == AV_CODEC_ID_PNG) || (m_in.m_pVideo_codec_ctx->codec_id == AV_CODEC_ID_MJPEG))
        {
            bIsVideo = false;

#ifdef USING_LIBAV
            if (m_in.m_pVideo_stream->avg_frame_rate.den)
            {
                double dbFrameRate = (double)m_in.m_pVideo_stream->avg_frame_rate.num / m_in.m_pVideo_stream->avg_frame_rate.den;

                // If frame rate is < 100 fps this should be a video
                if (dbFrameRate < 100)
                {
                    bIsVideo = true;
                }
            }
#else
            if (m_in.m_pVideo_stream->r_frame_rate.den)
            {
                double dbFrameRate = (double)m_in.m_pVideo_stream->r_frame_rate.num / m_in.m_pVideo_stream->r_frame_rate.den;

                // If frame rate is < 100 fps this should be a video
                if (dbFrameRate < 100)
                {
                    bIsVideo = true;
                }
            }
#endif
        }
        else
        {
            // If the source codec is not PNG or JPG we can safely assume it's a video stream
            bIsVideo = true;
        }
    }

    return bIsVideo;
}

bool FFMPEG_Transcoder::is_open() const
{
    return (m_in.m_pFormat_ctx != NULL);
}

/*
 * Open the given FFmpeg file and prepare for decoding. After this function,
 * the other methods can be used to process the file.
 */
int FFMPEG_Transcoder::open_input_file(const char* filename) {
    AVDictionary * opt = NULL;
    int ret;

    mp3fs_debug("Initialising FFmpeg transcoder for file '%s'.", filename);

    struct stat s;
    if (stat(filename, &s) < 0) {
        mp3fs_error("File stat failed for file '%s'.", filename);
        return -1;
    }
    m_mtime = s.st_mtime;

    if (is_open())
    {
        mp3fs_debug("File '%s' already open.", filename);
        return 0;
    }

    //    This allows selecting if the demuxer should consider all streams to be
    //    found after the first PMT and add further streams during decoding or if it rather
    //    should scan all that are within the analyze-duration and other limits

    ret = ::av_dict_set(&opt, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

    //    av_dict_set(&opt, "analyzeduration", "8000000", 0);  // <<== honored
    //    av_dict_set(&opt, "probesize", "8000000", 0);    //<<== honored

    if (ret < 0)
    {
        mp3fs_error("Error setting dictionary options (error '%s') file '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        return -1; // Couldn't open file
    }

    /** Open the input file to read from it. */
    assert(m_in.m_pFormat_ctx == NULL);
    ret = avformat_open_input(&m_in.m_pFormat_ctx, filename, NULL, &opt);
    if (ret < 0) {
        mp3fs_error("Could not open input file (error '%s') '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        return ret;
    }
    assert(m_in.m_pFormat_ctx != NULL);

    ret = ::av_dict_set(&opt, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    if (ret < 0)
    {
        mp3fs_error("Error setting dictionary options (error '%s') file '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        return -1; // Couldn't open file
    }

    AVDictionaryEntry * t;
    if ((t = av_dict_get(opt, "", NULL, AV_DICT_IGNORE_SUFFIX)))
    {
        mp3fs_error("Option %s not found for '%s'.", t->key, filename);
        return -1; // Couldn't open file
    }

#if (LIBAVFORMAT_VERSION_MICRO >= 100 && LIBAVFORMAT_VERSION_INT >= LIBAVCODEC_MIN_VERSION_INT )
    av_format_inject_global_side_data(m_in.m_pFormat_ctx);
#endif

    /** Get information on the input file (number of streams etc.). */
    ret = avformat_find_stream_info(m_in.m_pFormat_ctx, NULL);
    if (ret < 0) {
        mp3fs_error("Could not open find stream info (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        avformat_close_input(&m_in.m_pFormat_ctx);
        m_in.m_pFormat_ctx = NULL;
        return ret;
    }

    //     av_dump_format(m_in.m_pFormat_ctx, 0, filename, 0);

    // Open best match video codec
    ret = open_codec_context(&m_in.m_nVideo_stream_idx, &m_in.m_pVideo_codec_ctx, m_in.m_pFormat_ctx, AVMEDIA_TYPE_VIDEO, filename);
    if (ret < 0 && ret != AVERROR_STREAM_NOT_FOUND)    // Not an error
    {
        mp3fs_error("Failed to open video codec (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        //        avformat_close_input(&m_in.m_pFormat_ctx);
        //        m_in.m_pFormat_ctx = NULL;
        return ret;
    }

    if (m_in.m_nVideo_stream_idx >= 0) {
        // We have a video stream
        m_in.m_pVideo_stream = m_in.m_pFormat_ctx->streams[m_in.m_nVideo_stream_idx];

        m_bIsVideo = is_video();

#ifdef CODEC_CAP_TRUNCATED
        if(m_in.m_pVideo_codec_ctx->codec->capabilities & CODEC_CAP_TRUNCATED)
        {
            m_in.m_pVideo_codec_ctx->flags|= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */
        }
#else
#warning "Your FFMPEG distribution is missing CODEC_CAP_TRUNCATED flag. Probably requires fixing!"
#endif
    }

    // Open best match audio codec
    // Save the audio decoder context for easier access later.
    ret = open_codec_context(&m_in.m_nAudio_stream_idx, &m_in.m_pAudio_codec_ctx, m_in.m_pFormat_ctx, AVMEDIA_TYPE_AUDIO, filename);
    if (ret < 0 && ret != AVERROR_STREAM_NOT_FOUND)    // Not an error
    {
        mp3fs_error("Failed to open audio codec (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), filename);
        //        avformat_close_input(&m_in.m_pFormat_ctx);
        //        m_in.m_pFormat_ctx = NULL;
        return ret;
    }

    if (m_in.m_nAudio_stream_idx >= 0) {
        // We have an audio stream
        m_in.m_pAudio_stream = m_in.m_pFormat_ctx->streams[m_in.m_nAudio_stream_idx];
    }

    if (m_in.m_nAudio_stream_idx == -1 && m_in.m_nVideo_stream_idx == -1) {
        mp3fs_error("File contains neither a video nor an audio stream '%s'.", filename);
        return AVERROR(EINVAL);
    }

    return 0;
}

int FFMPEG_Transcoder::open_output_file(Buffer *buffer) {

    // Pre-allocate the predicted file size to reduce memory reallocations
    if (!buffer->reserve(calculate_size())) {
        mp3fs_error("Out of memory pre-allocating buffer for '%s'.", m_in.m_pFormat_ctx->filename);
        return -1;
    }

    /** Open the output file for writing. */
    if (open_output_filestreams(buffer)) {
        return -1;
    }

    if (m_out.m_nAudio_stream_idx > -1)
    {
        /** Initialize the resampler to be able to convert audio sample formats. */
        if (init_resampler()){
            return -1;
        }
        /** Initialize the FIFO buffer to store audio samples to be encoded. */
        if (init_fifo()){
            return -1;
        }
    }

    /*
     * Process metadata. The Decoder will call the Encoder to set appropriate
     * tag values for the output file.
     */
    if (process_metadata()) {
        return -1;
    }

    /** Write the header of the output file container. */
    if (write_output_file_header()){
        return -1;
    }

    return 0;
}

int64_t FFMPEG_Transcoder::get_output_bit_rate(AVStream *in_stream, int64_t max_bit_rate) const
{
    int64_t real_bit_rate = in_stream->codec->bit_rate != 0 ? in_stream->codec->bit_rate : m_in.m_pFormat_ctx->bit_rate;

    max_bit_rate *= 1000;   // kbit -> bit

    if (real_bit_rate > max_bit_rate)
    {
        real_bit_rate = max_bit_rate;
    }

    return real_bit_rate;
}

int FFMPEG_Transcoder::add_stream(AVCodecID codec_id)
{
    AVCodecContext *codec_ctx           = NULL;
    AVStream *      out_video_stream    = NULL;
    AVCodec *       output_codec        = NULL;
    AVDictionary *  opt                 = NULL;
    int ret;

    /* find the encoder */
    output_codec = avcodec_find_encoder(codec_id);
    if (!output_codec) {
        mp3fs_error("Could not find encoder '%s' for '%s'.", avcodec_get_name(codec_id),  m_in.m_pFormat_ctx->filename);
        exit(1);
    }

#if (LIBAVCODEC_VERSION_MAJOR > 56) // Check for FFmpeg 3
    out_video_stream = avformat_new_stream(m_out.m_pFormat_ctx, NULL);
#else
    out_video_stream = avformat_new_stream(m_out.m_pFormat_ctx, output_codec);
#endif
    if (!out_video_stream) {
        mp3fs_error("Could not allocate stream for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR(ENOMEM);
    }
    out_video_stream->id = m_out.m_pFormat_ctx->nb_streams-1;
#if (LIBAVCODEC_VERSION_MAJOR > 56) // Check for FFmpeg 3
    codec_ctx = avcodec_alloc_context3(output_codec);
    if (!codec_ctx) {
        mp3fs_error("Could not alloc an encoding context for '%s'.", m_in.m_pFormat_ctx->filename);;
        return AVERROR(ENOMEM);
    }
#else
    codec_ctx = out_video_stream->codec;
#endif

    switch (output_codec->type) {
    case AVMEDIA_TYPE_AUDIO:
    {
        /**
         * Set the basic encoder parameters.
         */
        codec_ctx->bit_rate                 = (int)get_output_bit_rate(m_in.m_pAudio_stream, params.audiobitrate);
        codec_ctx->channels                 = 2;
        codec_ctx->channel_layout           = av_get_default_channel_layout(codec_ctx->channels);
        codec_ctx->sample_rate              = m_in.m_pAudio_codec_ctx->sample_rate;
        if (params.audiosamplerate && codec_ctx->sample_rate > (int)params.audiosamplerate)
        {
            // Limit sample rate
            mp3fs_info("Limiting audio sample rate from %i Hz to %i Hz for '%s'.", codec_ctx->sample_rate, params.audiosamplerate, m_in.m_pFormat_ctx->filename);
            codec_ctx->sample_rate          = params.audiosamplerate;
        }
        codec_ctx->sample_fmt               = output_codec->sample_fmts[0];

        /** Allow the use of the experimental AAC encoder */
        codec_ctx->strict_std_compliance    = FF_COMPLIANCE_EXPERIMENTAL;

        /** Set the sample rate for the container. */
        out_video_stream->time_base.den     = codec_ctx->sample_rate;
        out_video_stream->time_base.num     = 1;

#if (LIBAVCODEC_VERSION_MAJOR <= 56) // Check for FFmpeg 3
        // set -strict -2 for aac (required for FFmpeg 2)
        av_dict_set(&opt, "strict", "-2", 0);
        // Allow the use of the experimental AAC encoder
        codec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
#endif

        /** Save the encoder context for easier access later. */
        m_out.m_pAudio_codec_ctx            = codec_ctx;
        // Save the stream index
        m_out.m_nAudio_stream_idx           = out_video_stream->index;
        // Save output audio stream for faster reference
        m_out.m_pAudio_stream               = out_video_stream;
        break;
    }
    case AVMEDIA_TYPE_VIDEO:
    {
        codec_ctx->codec_id = codec_id;

        /**
         * Set the basic encoder parameters.
         * The input file's sample rate is used to avoid a sample rate conversion.
         */
        AVStream *in_video_stream = m_in.m_pVideo_stream;

        //        ret = avcodec_parameters_from_context(stream->codecpar, m_in.m_pVideo_codec_ctx);
        //        if (ret < 0) {
        //            return ret;
        //        }

        codec_ctx->bit_rate             = (int)get_output_bit_rate(m_in.m_pVideo_stream, params.videobitrate);
        //codec_ctx->bit_rate_tolerance   = 0;
        /* Resolution must be a multiple of two. */
        codec_ctx->width                = in_video_stream->codec->width;
        codec_ctx->height               = in_video_stream->codec->height;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        //stream->time_base               = in_video_stream->time_base;
        out_video_stream->time_base               = { 1, 90000 }; // ??? Fest setzen für MP4?
        codec_ctx->time_base            = out_video_stream->time_base;
        // At this moment the output format must be AV_PIX_FMT_YUV420P;
        codec_ctx->pix_fmt       		= AV_PIX_FMT_YUV420P;

        if (in_video_stream->codec->pix_fmt != codec_ctx->pix_fmt ||
                in_video_stream->codec->width != codec_ctx->width ||
                in_video_stream->codec->height != codec_ctx->height)
        {
            // Rescal image if required
            const AVPixFmtDescriptor *fmtin = av_pix_fmt_desc_get((AVPixelFormat)in_video_stream->codec->pix_fmt);
            const AVPixFmtDescriptor *fmtout = av_pix_fmt_desc_get(codec_ctx->pix_fmt);
            mp3fs_debug("Initialising pixel format conversion %s to %s for '%s'.", fmtin != NULL ? fmtin->name : "-", fmtout != NULL ? fmtout->name : "-", m_in.m_pFormat_ctx->filename);
            assert(m_pSws_ctx == NULL);
            m_pSws_ctx = sws_getContext(
                        // Source settings
                        in_video_stream->codec->width,          // width
                        in_video_stream->codec->height,         // height
                        in_video_stream->codec->pix_fmt,        // format
                        // Target settings
                        codec_ctx->width,                       // width
                        codec_ctx->height,                      // height
                        codec_ctx->pix_fmt,                     // format
                        SWS_BICUBIC, NULL, NULL, NULL);
            if (!m_pSws_ctx)
            {
                mp3fs_error("Could not allocate scaling/conversion context for '%s'.", m_in.m_pFormat_ctx->filename);
                return AVERROR(ENOMEM);
            }
        }

        codec_ctx->gop_size             = 12;   // emit one intra frame every twelve frames at most

#ifdef USING_LIBAV
#warning "Must be fixed here! USING_LIBAV"
#else
        codec_ctx->framerate            = in_video_stream->codec->framerate;
        if (!codec_ctx->framerate.num) {
            codec_ctx->framerate = (AVRational){ 25, 1 };
            mp3fs_warning(
                        "No information "
                        "about the input framerate is available. Falling "
                        "back to a default value of 25fps for output stream");
        }
        out_video_stream->avg_frame_rate = codec_ctx->framerate;
#endif
        codec_ctx->sample_aspect_ratio  = in_video_stream->codec->sample_aspect_ratio;

        // FÜR ALBUM ARTS?
        // mp4 album arts do not work with ipod profile. Set mp4.
        //    if (m_out.m_pFormat_ctx->oformat->mime_type != NULL && (!strcmp(m_out.m_pFormat_ctx->oformat->mime_type, "application/mp4") || !strcmp(m_out.m_pFormat_ctx->oformat->mime_type, "video/mp4")))
        //    {
        //        m_out.m_pFormat_ctx->oformat->name = "mp4";
        //        m_out.m_pFormat_ctx->oformat->mime_type = "application/mp4";
        //    }

        //    // Ignore missing width/height
        //    m_out.m_pFormat_ctx->oformat->flags |= AVFMT_NODIMENSIONS;

        //    codec_ctx->flags2 |= AV_CODEC_FLAG2_FAST;

        // -profile:v baseline -level 3.0
        //av_opt_set(codec_ctx->priv_data, "profile", "baseline", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set(codec_ctx->priv_data, "level", "3.0", AV_OPT_SEARCH_CHILDREN);
        // -profile:v high -level 3.1
        av_opt_set(codec_ctx->priv_data, "profile", "high", AV_OPT_SEARCH_CHILDREN);
        av_opt_set(codec_ctx->priv_data, "level", "3.1", AV_OPT_SEARCH_CHILDREN);

        av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set(codec_ctx->priv_data, "preset", "veryfast", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", AV_OPT_SEARCH_CHILDREN);

        //av_opt_set(codec_ctx->priv_data, "qmin", "0", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set(codec_ctx->priv_data, "qmax", "69", AV_OPT_SEARCH_CHILDREN);
        //av_opt_set(codec_ctx->priv_data, "qdiff", "4", AV_OPT_SEARCH_CHILDREN);

        //        if (!av_dict_get(codec_ctx->priv_data, "threads", NULL, 0))
        //        {
        //            av_dict_set(codec_ctx->priv_data, "threads", "auto", 0);
        //        }

        /** Save the encoder context for easier access later. */
        m_out.m_pVideo_codec_ctx    = codec_ctx;
        // Save the stream index
        m_out.m_nVideo_stream_idx = out_video_stream->index;
        // Save output video stream for faster reference
        m_out.m_pVideo_stream = out_video_stream;
        break;
    }
    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (m_out.m_pFormat_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    /** Open the encoder for the audio stream to use it later. */
    if ((ret = avcodec_open2(codec_ctx, output_codec, &opt)) < 0) {
        mp3fs_error("Could not open audio output codec (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        avcodec_free_context(&codec_ctx);
        return ret;
    }

    mp3fs_debug("successfully opened %s output codec for '%s'.", get_codec_name(codec_id), m_in.m_pFormat_ctx->filename);

#if (LIBAVCODEC_VERSION_MAJOR > 56) // Check for FFmpeg 3
    ret = avcodec_parameters_from_context(out_video_stream->codecpar, codec_ctx);
    if (ret < 0) {
        mp3fs_error("Could not initialise stream parameters (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        avcodec_free_context(&codec_ctx);
        return ret;
    }
#endif

    return 0;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 */
int FFMPEG_Transcoder::open_output_filestreams(Buffer *buffer)
{
    AVIOContext *   output_io_context   = NULL;
    AVCodecID       audio_codecid;
    AVCodecID       video_codecid;
    const char*     type = params.desttype;
    const char *    ext;
    int             ret                 = 0;

    if (!strcasecmp(type, "mp3"))
    {
        ext = "mp3";
        audio_codecid = AV_CODEC_ID_MP3;
        video_codecid = AV_CODEC_ID_PNG;
        m_out.m_output_type = TYPE_MP3;
    }
    else if (!strcasecmp(type, "mp4"))
    {
#ifndef DISABLE_ISMV
        ext = params.enable_ismv ? "ismv" : "mp4";
#else
        ext = "mp4";
#endif
        audio_codecid = AV_CODEC_ID_AAC;
        video_codecid = AV_CODEC_ID_H264;
        // video_codecid = AV_CODEC_ID_MJPEG;
        m_out.m_output_type = TYPE_MP4;
    }
    else
    {
        mp3fs_error("Unknown format type '%s' for '%s'.", type, m_in.m_pFormat_ctx->filename);
        return -1;
    }

    mp3fs_debug("Opening format type '%s' for '%s'.", type, m_in.m_pFormat_ctx->filename);

    /** Create a new format context for the output container format. */
    avformat_alloc_output_context2(&m_out.m_pFormat_ctx, NULL, ext, NULL);
    if (!m_out.m_pFormat_ctx) {
        mp3fs_error("Could not allocate output format context for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR(ENOMEM);
    }

    if (!m_bIsVideo)
    {
        m_in.m_nVideo_stream_idx = INVALID_STREAM;  // TEST
    }

    //video_codecid = m_out.m_pFormat_ctx->oformat->video_codec;

    if (m_in.m_nVideo_stream_idx != INVALID_STREAM)
    {
        ret = add_stream(video_codecid);
        if (ret < 0) {
            return ret;
        }
    }

    //    m_in.m_nAudio_stream_idx = INVALID_STREAM;

    //audio_codecid = m_out.m_pFormat_ctx->oformat->audio_codec;

    if (m_in.m_nAudio_stream_idx != INVALID_STREAM)
    {
        ret = add_stream(audio_codecid);
        if (ret < 0) {
            return ret;
        }
    }

    /* open the output file */
    int nBufSize = 1024;
    output_io_context = ::avio_alloc_context(
                (unsigned char *) ::av_malloc(nBufSize + FF_INPUT_BUFFER_PADDING_SIZE),
                nBufSize,
                1,
                (void *)buffer,
                NULL /*readPacket*/,
                writePacket,
                seek);

    /** Associate the output file (pointer) with the container format context. */
    m_out.m_pFormat_ctx->pb = output_io_context;

    // Some formats require the time stamps to start at 0, so if there is a difference between
    // the streams we need to drop audio or video until we are in sync.
    if ((m_in.m_nVideo_stream_idx != INVALID_STREAM) &&
            (m_in.m_nAudio_stream_idx != INVALID_STREAM))
    {
        // Calculate difference
        AVStream *in_audio_stream = m_in.m_pAudio_stream;
        AVStream *in_video_stream = m_in.m_pVideo_stream;

        int64_t audio_start = av_rescale_q(in_audio_stream->start_time, in_audio_stream->time_base, AV_TIME_BASE_Q);

        m_out.m_video_start_pts = av_rescale_q(audio_start, AV_TIME_BASE_Q, in_video_stream->time_base);
    }

    return 0;
}

/** Initialize one data packet for reading or writing. */
void FFMPEG_Transcoder::init_packet(AVPacket *packet)
{
    av_init_packet(packet);
    /** Set the packet data and size so that it is recognized as being empty. */
    packet->data = NULL;
    packet->size = 0;
}

/** Initialize one audio frame for reading from the input file */
int FFMPEG_Transcoder::init_input_frame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        mp3fs_error("Could not allocate input frame for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libavresample takes care of this, but requires initialisation.
         */
int FFMPEG_Transcoder::init_resampler()
{
    /**
      * Only initialise the resampler if it is necessary, i.e.,
      * if and only if the sample formats differ.
      */
    if (m_in.m_pAudio_codec_ctx->sample_fmt != m_out.m_pAudio_codec_ctx->sample_fmt ||
            m_in.m_pAudio_codec_ctx->sample_rate != m_out.m_pAudio_codec_ctx->sample_rate ||
            m_in.m_pAudio_codec_ctx->channels != m_out.m_pAudio_codec_ctx->channels) {
        int ret;

        /** Create a resampler context for the conversion. */
        if (!(m_pAudio_resample_ctx = avresample_alloc_context())) {
            mp3fs_error("Could not allocate resample context for '%s'.", m_in.m_pFormat_ctx->filename);
            return AVERROR(ENOMEM);
        }

        /**
         * Set the conversion parameters.
         * Default channel layouts based on the number of channels
         * are assumed for simplicity (they are sometimes not detected
         * properly by the demuxer and/or decoder).
                 */
        av_opt_set_int(m_pAudio_resample_ctx, "in_channel_layout", av_get_default_channel_layout(m_in.m_pAudio_codec_ctx->channels), 0);
        av_opt_set_int(m_pAudio_resample_ctx, "out_channel_layout", av_get_default_channel_layout(m_out.m_pAudio_codec_ctx->channels), 0);
        av_opt_set_int(m_pAudio_resample_ctx, "in_sample_rate", m_in.m_pAudio_codec_ctx->sample_rate, 0);
        av_opt_set_int(m_pAudio_resample_ctx, "out_sample_rate", m_out.m_pAudio_codec_ctx->sample_rate, 0);
        av_opt_set_int(m_pAudio_resample_ctx, "in_sample_fmt", m_in.m_pAudio_codec_ctx->sample_fmt, 0);
        av_opt_set_int(m_pAudio_resample_ctx, "out_sample_fmt", m_out.m_pAudio_codec_ctx->sample_fmt, 0);

        /** Open the resampler with the specified parameters. */
        if ((ret = avresample_open(m_pAudio_resample_ctx)) < 0) {
            mp3fs_error("Could not open resampler context (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            avresample_free(&m_pAudio_resample_ctx);
            m_pAudio_resample_ctx = NULL;
            return ret;
        }
    }
    return 0;
}

/** Initialize a FIFO buffer for the audio samples to be encoded. */
int FFMPEG_Transcoder::init_fifo()
{
    /** Create the FIFO buffer based on the specified output sample format. */
    if (!(m_pAudioFifo = av_audio_fifo_alloc(m_out.m_pAudio_codec_ctx->sample_fmt, m_out.m_pAudio_codec_ctx->channels, 1))) {
        mp3fs_error("Could not allocate FIFO for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR(ENOMEM);
    }
    return 0;
}

/** Write the header of the output file container. */
int FFMPEG_Transcoder::write_output_file_header()
{
    int ret;
    AVDictionary* dict = nullptr;

    if (m_out.m_output_type == TYPE_MP4)
    {
#ifndef DISABLE_ISMV
        if (!params.enable_ismv)
        {
#endif
            // For all but M$ Explorer/Edge
            // Settings for fast playback start in HTML5
            av_dict_set(&dict, "movflags", "+faststart", 0);
            av_dict_set(&dict, "movflags", "+empty_moov", 0);
            //av_dict_set(&dict, "movflags", "+separate_moof", 0); // MS Edge
            av_dict_set(&dict, "frag_duration", "1000000", 0); // 1 sec
            //av_dict_set(&dict, "frag_duration", "10000000", 0);

            // Keine guten Ideen
            //av_dict_set(&dict, "movflags", "frag_keyframe", 0);
            //av_dict_set(&dict, "movflags", "default_base_moof", 0);
            //av_dict_set(&dict, "movflags", "omit_tfhd_offset", 0)
            //av_dict_set(&dict, "movflags", "+rtphint", 0);
            //av_dict_set(&dict, "movflags", "+disable_chpl", 0);

            // Geht (empty_moov+empty_moov automatisch mit isml)
            //av_dict_set(&dict, "movflags", "isml+frag_keyframe+separate_moof", 0);
            //av_dict_set(&dict, "movflags", "isml", 0);

            // spielt 50 Sekunden...
            // ISMV NICHT av_dict_set(&dict, "frag_duration", "50000000", 0);
#ifndef DISABLE_ISMV
        }
        else
        {
            // For M$ Explorer/Edge
            // Settings for fast playback start in HTML5
            av_dict_set(&dict, "movflags", "+faststart", 0);
            // Geht (empty_moov+empty_moov automatisch mit isml)
            av_dict_set(&dict, "movflags", "isml+frag_keyframe+separate_moof", 0);
            av_dict_set(&dict, "frag_duration", "5000000", 0); // 1 sec
        }
#endif

        av_dict_set(&dict, "flags:a", "+global_header", 0);
        av_dict_set(&dict, "flags:v", "+global_header", 0);
    }

#ifdef AVSTREAM_INIT_IN_WRITE_HEADER
    if (avformat_init_output(m_out.m_pFormat_ctx, &dict) == AVSTREAM_INIT_IN_WRITE_HEADER) {
#endif // AVSTREAM_INIT_IN_WRITE_HEADER
        if ((ret = avformat_write_header(m_out.m_pFormat_ctx, &dict)) < 0) {
            mp3fs_error("Could not write output file header (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            return ret;
        }
#ifdef AVSTREAM_INIT_IN_WRITE_HEADER
    }
#endif // AVSTREAM_INIT_IN_WRITE_HEADER

    return 0;
}

AVFrame *FFMPEG_Transcoder::alloc_picture(AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        mp3fs_error("Could not allocate frame data for '%s'.", m_in.m_pFormat_ctx->filename);
        av_frame_free(&picture);
        return NULL;
    }

    return picture;
}

// TODO
//static int flush_encoder(unsigned int stream_index)
//{
//    int ret;
//    int got_frame;

//    if (!(stream_ctx[stream_index].enc_ctx->codec->capabilities &
//          AV_CODEC_CAP_DELAY))
//        return 0;

//    while (1) {
//        av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
//        ret = encode_write_frame(NULL, stream_index, &got_frame);
//        if (ret < 0)
//            break;
//        if (!got_frame)
//            return 0;
//    }
//    return ret;
//}


/** Decode one audio frame from the input file. */
int FFMPEG_Transcoder::decode_frame(AVPacket *input_packet, int *data_present)
{
    int decoded = 0;

    if (input_packet->stream_index == m_in.m_nAudio_stream_idx)
    {
        /** Temporary storage of the input samples of the frame read from the file. */
        AVFrame *input_frame = NULL;
        int ret = 0;

        /** Initialize temporary storage for one input frame. */
        ret = init_input_frame(&input_frame);
        if (ret < 0)
        {
            av_frame_free(&input_frame);
            return ret;
        }

        /**
         * Decode the audio frame stored in the temporary packet.
         * The input audio stream decoder is used to do this.
         * If we are at the end of the file, pass an empty packet to the decoder
         * to flush it.
         */
        ret = avcodec_decode_audio4(m_in.m_pAudio_codec_ctx, input_frame, data_present, input_packet);
        if (ret < 0 && ret != AVERROR_INVALIDDATA) {
            av_frame_free(&input_frame);
            mp3fs_error("Could not decode audio frame (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            return ret;
        }

        if (ret != AVERROR_INVALIDDATA)
        {
            decoded = ret;
        }
        else
        {
            decoded = input_packet->size;
        }

        /** If there is decoded data, convert and store it */
        if (data_present && input_frame->nb_samples) {
            /** Temporary storage for the converted input samples. */
            uint8_t **converted_input_samples = NULL;
            int nb_output_samples = (m_pAudio_resample_ctx != NULL) ? avresample_get_out_samples(m_pAudio_resample_ctx, input_frame->nb_samples) : input_frame->nb_samples;
            // Store audio frame
            /** Initialize the temporary storage for the converted input samples. */
            ret = init_converted_samples(&converted_input_samples, nb_output_samples);
            if (ret < 0)
            {
                goto cleanup2;
            }

            /**
             * Convert the input samples to the desired output sample format.
             * This requires a temporary storage provided by converted_input_samples.
             */
            ret = convert_samples(input_frame->extended_data, input_frame->nb_samples, converted_input_samples, &nb_output_samples);
            if (ret < 0)
            {
                goto cleanup2;
            }

            /** Add the converted input samples to the FIFO buffer for later processing. */
            ret = add_samples_to_fifo(converted_input_samples, nb_output_samples);
            if (ret < 0)
            {
                goto cleanup2;
            }
            ret = 0;
cleanup2:
            if (converted_input_samples)
            {
                av_freep(&converted_input_samples[0]);
                free(converted_input_samples);
            }
        }

        //cleanup:

        av_frame_free(&input_frame);
    }
    else if (input_packet->stream_index == m_in.m_nVideo_stream_idx)
    {
        AVFrame *input_frame = NULL;
        int ret = 0;

        /** Initialize temporary storage for one input frame. */
        ret = init_input_frame(&input_frame);
        if (ret < 0)
        {
            return ret;
        }

        /* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
           and this is the only method to use them because you cannot
           know the compressed data size before analysing it.

           BUT some other codecs (msmpeg4, mpeg4) are inherently frame
           based, so you must call them with all the data for one
           frame exactly. You must also initialise 'width' and
           'height' before initialising them. */

        /* NOTE2: some codecs allow the raw parameters (frame size,
           sample rate) to be changed at any frame. We handle this, so
           you should also take care of it */

        /* here, we use a stream based decoder (mpeg1video), so we
           feed decoder and see if it could decode a frame */

        ret = avcodec_decode_video2(m_in.m_pVideo_codec_ctx, input_frame, data_present, input_packet);

        if (ret < 0) {
            mp3fs_error("Could not decode video frame (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            // unused frame
            av_frame_free(&input_frame);
            return ret;
        }

        decoded = ret;

        // Sometimes only a few packets contain valid dts/pts/pos data, so we keep it
        if (input_packet->dts != (int64_t)AV_NOPTS_VALUE)
        {
            int64_t pts = input_packet->dts;
            if (pts > m_pts)
            {
                m_pts = pts;
            }
        }
        else if (input_packet->pts != (int64_t)AV_NOPTS_VALUE)
        {
            int64_t pts = input_packet->pts;
            if (pts > m_pts)
            {
                m_pts = pts;
            }
        }

        if (input_packet->pos > -1)
        {
            m_pos = input_packet->pos;
        }

        if (*data_present)
        {
            if (m_pSws_ctx != NULL)
            {
                AVCodecContext *c = m_out.m_pVideo_codec_ctx;

                AVFrame * tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
                if (!tmp_frame) {
                    return AVERROR(ENOMEM);
                }

                sws_scale(m_pSws_ctx,
                          (const uint8_t * const *)input_frame->data, input_frame->linesize,
                          0, c->height,
                          tmp_frame->data, tmp_frame->linesize);

                tmp_frame->pts = input_frame->pts;

                av_frame_free(&input_frame);

                input_frame = tmp_frame;
            }

#ifndef USING_LIBAV
            int64_t best_effort_timestamp = ::av_frame_get_best_effort_timestamp(input_frame);

            if (best_effort_timestamp != (int64_t)AV_NOPTS_VALUE)
            {
                input_frame->pts = best_effort_timestamp;
            }
#endif

            if (input_frame->pts == (int64_t)AV_NOPTS_VALUE)
            {
                input_frame->pts = m_pts;
            }

            // Rescale to our time base, but only of nessessary
            if (input_frame->pts != (int64_t)AV_NOPTS_VALUE && (m_in.m_pVideo_stream->time_base.den != m_out.m_pVideo_stream->time_base.den || m_in.m_pVideo_stream->time_base.num != m_out.m_pVideo_stream->time_base.num))
            {
                input_frame->pts = av_rescale_q_rnd(input_frame->pts, m_in.m_pVideo_stream->time_base, m_out.m_pVideo_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            }

            m_VideoFifo.push(input_frame);
        }
        else
        {
            // unused frame
            av_frame_free(&input_frame);
        }
    }
    else
    {
        *data_present = 0;
        decoded = input_packet->size;    // Ignore
    }

    return decoded;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 */
int FFMPEG_Transcoder::init_converted_samples(uint8_t ***converted_input_samples, int frame_size)
{
    int ret;

    /**
     * Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples = (uint8_t **)calloc(m_out.m_pAudio_codec_ctx->channels, sizeof(**converted_input_samples)))) {
        mp3fs_error("Could not allocate converted input sample pointers for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR(ENOMEM);
    }

    /**
     * Allocate memory for the samples of all channels in one consecutive
     * block for convenience.
     */
    if ((ret = av_samples_alloc(*converted_input_samples, NULL,
                                m_out.m_pAudio_codec_ctx->channels,
                                frame_size,
                                m_out.m_pAudio_codec_ctx->sample_fmt, 0)) < 0) {
        mp3fs_error("Could not allocate converted input samples  (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return ret;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is specified
 * by frame_size.
 */
int FFMPEG_Transcoder::convert_samples(uint8_t **input_data, const int in_samples, uint8_t **converted_data, int *out_samples)
{
    if (m_pAudio_resample_ctx != NULL)
    {
        int ret;

        /** Convert the samples using the resampler. */
        if ((ret = avresample_convert(m_pAudio_resample_ctx, converted_data, 0, *out_samples, input_data, 0, in_samples)) < 0) {
            mp3fs_error("Could not convert input samples (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            return ret;
        }

        *out_samples = ret;

        /**
          * Perform a sanity check so that the number of converted samples is
          * not greater than the number of samples to be converted.
          * If the sample rates differ, this case has to be handled differently
          */
        if (avresample_available(m_pAudio_resample_ctx)) {
            mp3fs_error("Converted samples left over for '%s'.", m_in.m_pFormat_ctx->filename);
            return AVERROR_EXIT;
        }
    }
    else
    {
        // No resampling, just copy samples
        if (!av_sample_fmt_is_planar(m_in.m_pAudio_codec_ctx->sample_fmt))
        {
            memcpy(converted_data[0], input_data[0], in_samples * av_get_bytes_per_sample(m_out.m_pAudio_codec_ctx->sample_fmt));
        }
        else
        {
            for (int n = 0; n < m_in.m_pAudio_codec_ctx->channels; n++)
            {
                memcpy(converted_data[n], input_data[n], in_samples * av_get_bytes_per_sample(m_out.m_pAudio_codec_ctx->sample_fmt));
            }
        }
    }
    return 0;
}

/** Add converted input audio samples to the FIFO buffer for later processing. */
int FFMPEG_Transcoder::add_samples_to_fifo(uint8_t **converted_input_samples, const int frame_size)
{
    int ret;

    /**
     * Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples.
     */
    if ((ret = av_audio_fifo_realloc(m_pAudioFifo, av_audio_fifo_size(m_pAudioFifo) + frame_size)) < 0) {
        mp3fs_error("Could not reallocate FIFO for '%s'.", m_in.m_pFormat_ctx->filename);
        return ret;
    }

    /** Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(m_pAudioFifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        mp3fs_error("Could not write data to FIFO for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR_EXIT;
    }
    return 0;
}

/**
 * Read one audio/video frame from the input file, decodes, converts and stores
 * it in the corresponding FIFO buffers.
 */
int FFMPEG_Transcoder::flush_frames(int stream_index, int *data_present)
{
    int ret = 0;

    if (stream_index > -1)
    {
        AVPacket flush_packet;

        init_packet(&flush_packet);

        flush_packet.data = NULL;
        flush_packet.size = 0;
        flush_packet.stream_index = stream_index;

        do
        {
            ret = decode_frame(&flush_packet, data_present);
            if (ret < 0)
            {
                break;
            }
        } while (*data_present);

        av_packet_unref(&flush_packet);
    }

    return ret;
}

int FFMPEG_Transcoder::read_decode_convert_and_store(int *finished)
{
    /** Packet used for temporary storage. */
    AVPacket input_packet;
    int data_present;
    int ret = AVERROR_EXIT;

    try
    {
        /** Read one audio frame from the input file into a temporary packet. */
        if ((ret = av_read_frame(m_in.m_pFormat_ctx, &input_packet)) < 0) {
            /** If we are the the end of the file, flush the decoder below. */
            if (ret == AVERROR_EOF)
            {
                *finished = 1;
            }
            else
            {
                mp3fs_error("Could not read frame (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
                throw false;
            }
        }

        if (!*finished)
        {
            do
            {
                // Decode one frame.
                ret = decode_frame(&input_packet, &data_present);
                if (ret < 0)
                {
                    throw false;
                }
                input_packet.data += ret;
                input_packet.size -= ret;
            } while (input_packet.size > 0);
        }
        else
        {
            // Flush cached frames, ignoring any errors
            if (m_in.m_pAudio_codec_ctx != NULL && m_in.m_pAudio_codec_ctx->codec->capabilities & AV_CODEC_CAP_DELAY)
            {
                flush_frames(m_in.m_nAudio_stream_idx, &data_present);
            }
            if (m_in.m_pVideo_codec_ctx != NULL && m_in.m_pVideo_codec_ctx->codec->capabilities & AV_CODEC_CAP_DELAY)
            {
                flush_frames(m_in.m_nVideo_stream_idx, &data_present);
            }
            
            data_present = 0;
        }

        ret = 0;
    }
    catch (bool)
    {
    }

    av_packet_unref(&input_packet);

    return ret;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 */
int FFMPEG_Transcoder::init_audio_output_frame(AVFrame **frame, int frame_size)
{
    int ret;

    /** Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        mp3fs_error("Could not allocate output frame for '%s'.", m_in.m_pFormat_ctx->filename);
        return AVERROR_EXIT;
    }

    /**
     * Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity.
     */
    (*frame)->nb_samples     = frame_size;
    (*frame)->channel_layout = m_out.m_pAudio_codec_ctx->channel_layout;
    (*frame)->format         = m_out.m_pAudio_codec_ctx->sample_fmt;
    (*frame)->sample_rate    = m_out.m_pAudio_codec_ctx->sample_rate;

    /**
     * Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified.
     */
    if ((ret = av_frame_get_buffer(*frame, 0)) < 0) {
        mp3fs_error("Could allocate output frame samples  (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        av_frame_free(frame);
        return ret;
    }

    return 0;
}

void FFMPEG_Transcoder::produce_dts(AVPacket *pkt, int64_t *pts)
{
    //    if ((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE)
    {
        int64_t duration;
        // Some encoders to not produce dts/pts.
        // So we make some up.
        assert(pkt->duration > 0);
        if (pkt->duration)
        {
            duration = pkt->duration;
        }
        else
        {
            duration = 1;
        }

        pkt->dts = *pts; // - duration;
        pkt->pts = *pts;

        *pts += duration;
    }
}

/** Encode one frame worth of audio to the output file. */
int FFMPEG_Transcoder::encode_audio_frame(AVFrame *frame, int *data_present)
{
    /** Packet used for temporary storage. */
    AVPacket output_packet;
    int ret;
    init_packet(&output_packet);

    /**
      * Encode the audio frame and store it in the temporary packet.
      * The output audio stream encoder is used to do this.
      */
    if ((ret = avcodec_encode_audio2(m_out.m_pAudio_codec_ctx, &output_packet, frame, data_present)) < 0) {
        mp3fs_error("Could not encode audio frame (error '%s') for file '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        av_packet_unref(&output_packet);
        return ret;
    }

    /** Write one audio frame from the temporary packet to the output file. */
    if (*data_present) {
        output_packet.stream_index = m_out.m_nAudio_stream_idx;

        produce_dts(&output_packet, &m_out.m_nAudio_pts);

        if ((ret = av_interleaved_write_frame(m_out.m_pFormat_ctx, &output_packet)) < 0) {
            mp3fs_error("Could not write audio frame (error '%s') for file '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            av_packet_unref(&output_packet);
            return ret;
        }

        av_packet_unref(&output_packet);
    }

    return 0;
}

/** Encode one frame worth of audio to the output file. */
int FFMPEG_Transcoder::encode_video_frame(AVFrame *frame, int *data_present)
{
    /** Packet used for temporary storage. */
    AVPacket output_packet;
    int ret;
    init_packet(&output_packet);

    if (frame != NULL)
    {
#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )

        //if (m_out.m_pVideo_codec_ctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME) && ost->top_field_first >= 0)
        //      frame->top_field_first = !!ost->top_field_first;

        if (frame->interlaced_frame)
        {
            if (m_out.m_pVideo_codec_ctx->codec->id == AV_CODEC_ID_MJPEG)
            {
                m_out.m_pVideo_stream->codecpar->field_order = frame->top_field_first ? AV_FIELD_TT:AV_FIELD_BB;
            }
            else
            {
                m_out.m_pVideo_stream->codecpar->field_order = frame->top_field_first ? AV_FIELD_TB:AV_FIELD_BT;
            }
        }
        else
        {
            m_out.m_pVideo_stream->codecpar->field_order = AV_FIELD_PROGRESSIVE;
        }
#endif

        frame->quality = m_out.m_pVideo_codec_ctx->global_quality;
#ifndef USING_LIBAV
        frame->pict_type = AV_PICTURE_TYPE_NONE;	// other than AV_PICTURE_TYPE_NONE causes warnings
#else
        frame->pict_type = (AVPictureType)0;        // other than 0 causes warnings
#endif
    }

    /**
     * Encode the video frame and store it in the temporary packet.
     * The output video stream encoder is used to do this.
     */
    if ((ret = avcodec_encode_video2(m_out.m_pVideo_codec_ctx, &output_packet, frame, data_present)) < 0) {
        mp3fs_error("Could not encode video frame (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        av_packet_unref(&output_packet);
        return ret;
    }

    /** Write one video frame from the temporary packet to the output file. */
    if (*data_present) {

        if (output_packet.pts != (int64_t)AV_NOPTS_VALUE)
        {
            output_packet.pts -=  m_out.m_video_start_pts;
        }
        if (output_packet.dts != (int64_t)AV_NOPTS_VALUE)
        {
            output_packet.dts -=  m_out.m_video_start_pts;
        }

        if (!(m_out.m_pFormat_ctx->oformat->flags & AVFMT_NOTIMESTAMPS)) {
            if (output_packet.dts != (int64_t)AV_NOPTS_VALUE &&
                    output_packet.pts != (int64_t)AV_NOPTS_VALUE &&
                    output_packet.dts > output_packet.pts) {

                mp3fs_warning("Invalid DTS: %" PRId64 " PTS: %" PRId64 " in video output, replacing by guess for '%s'.", output_packet.dts, output_packet.pts, m_in.m_pFormat_ctx->filename);

                output_packet.pts =
                        output_packet.dts = output_packet.pts + output_packet.dts + m_out.m_last_mux_dts + 1
                        - FFMIN3(output_packet.pts, output_packet.dts, m_out.m_last_mux_dts + 1)
                        - FFMAX3(output_packet.pts, output_packet.dts, m_out.m_last_mux_dts + 1);
            }
            if (output_packet.dts != (int64_t)AV_NOPTS_VALUE && m_out.m_last_mux_dts != (int64_t)AV_NOPTS_VALUE)
            {
                int64_t max = m_out.m_last_mux_dts + !(m_out.m_pFormat_ctx->oformat->flags & AVFMT_TS_NONSTRICT);
                if (output_packet.dts < max) {

                    mp3fs_warning("Non-monotonous DTS in video output stream; previous: %" PRId64 ", current: %" PRId64 "; changing to %" PRId64 ". This may result in incorrect timestamps in the output for '%s'.", m_out.m_last_mux_dts, output_packet.dts, max, m_in.m_pFormat_ctx->filename);

                    if (output_packet.pts >= output_packet.dts)
                    {
                        output_packet.pts = FFMAX(output_packet.pts, max);
                    }
                    output_packet.dts = max;
                }
            }
        }
        m_out.m_last_mux_dts = output_packet.dts;

        ret = av_interleaved_write_frame(m_out.m_pFormat_ctx, &output_packet);
        if (ret < 0) {
            mp3fs_error("Could not write video frame (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
            av_packet_unref(&output_packet);
            return ret;
        }

        av_packet_unref(&output_packet);
    }

    return 0;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
         */
int FFMPEG_Transcoder::load_encode_and_write()
{
    /** Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
    /**
     * Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size
     */
    const int frame_size = FFMIN(av_audio_fifo_size(m_pAudioFifo), m_out.m_pAudio_codec_ctx->frame_size);
    int data_written;

    /** Initialize temporary storage for one output frame. */
    if (init_audio_output_frame(&output_frame, frame_size))
        return AVERROR_EXIT;

    /**
     * Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily.
     */
    if (av_audio_fifo_read(m_pAudioFifo, (void **)output_frame->data, frame_size) < frame_size) {
        mp3fs_error("Could not read data from FIFO for '%s'.", m_in.m_pFormat_ctx->filename);
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    /** Encode one frame worth of audio samples. */
    if (encode_audio_frame(output_frame, &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    av_frame_free(&output_frame);
    return 0;
}

/** Write the trailer of the output file container. */
int FFMPEG_Transcoder::write_output_file_trailer()
{
    int ret;
    if ((ret = av_write_trailer(m_out.m_pFormat_ctx)) < 0) {
        mp3fs_error("Could not write output file trailer  (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        return ret;
    }
    return 0;
}

time_t FFMPEG_Transcoder::mtime() const {
    return m_mtime;
}

/*
 * Process the metadata in the FFmpeg file. This should be called at the
 * beginning, before reading audio data. The set_text_tag() and
 * set_picture_tag() methods of the given Encoder will be used to set the
 * metadata, with results going into the given Buffer. This function will also
 * read the actual PCM stream parameters.
 */

#define tagcpy(dst, src)    \
    for (char *p1 = (dst), *pend = p1 + sizeof(dst), *p2 = (src); *p2 && p1 < pend; p1++, p2++) \
    *p1 = *p2;

void FFMPEG_Transcoder::copy_metadata(AVDictionary *metadata, AVStream * stream, bool bIsVideo) // TODO: Stream tags
{
    AVDictionaryEntry *tag = NULL;

    while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
    {
        if (stream != NULL && bIsVideo)
        {
            // Keep video comments in video stream
            // TODO
            continue;
        }

        av_dict_set(&m_out.m_pFormat_ctx->metadata, tag->key, tag->value, 0);

        if (m_out.m_output_type == TYPE_MP3)
        {
            // For MP3 fill in ID3v1 structure
            if (!strcasecmp(tag->key, "ARTIST"))
            {
                tagcpy(m_out.m_id3v1.m_sSongArtist, tag->value);
            }
            else if (!strcasecmp(tag->key, "TITLE"))
            {
                tagcpy(m_out.m_id3v1.m_sSongTitle, tag->value);
            }
            else if (!strcasecmp(tag->key, "ALBUM"))
            {
                tagcpy(m_out.m_id3v1.m_sAlbumName, tag->value);
            }
            else if (!strcasecmp(tag->key, "COMMENT"))
            {
                tagcpy(m_out.m_id3v1.m_sComment, tag->value);
            }
            else if (!strcasecmp(tag->key, "YEAR") || !strcasecmp(tag->key, "DATE"))
            {
                tagcpy(m_out.m_id3v1.m_sYear, tag->value);
            }
            else if (!strcasecmp(tag->key, "TRACK"))
            {
                m_out.m_id3v1.m_bTitleNo = (char)atoi(tag->value);
            }
        }
    }
}

int FFMPEG_Transcoder::process_metadata() {

    mp3fs_debug("Processing metadata for '%s'.", m_in.m_pFormat_ctx->filename);

    copy_metadata(m_in.m_pFormat_ctx->metadata, NULL, false);

    // For some formats (namely ogg) FFmpeg returns the tags, odd enough, with streams...
    if (m_in.m_pAudio_stream)
    {
        copy_metadata(m_in.m_pAudio_stream->metadata, m_in.m_pAudio_stream, false);
    }

    if (m_in.m_pVideo_stream)
    {
        copy_metadata(m_in.m_pVideo_stream->metadata, m_in.m_pVideo_stream, true);
    }

    // Pictures later. More complicated...

    return 0;
}

/*
 * Process a single frame of audio data. The encode_pcm_data() method
 * of the Encoder will be used to process the resulting audio data, with the
 * result going into the given Buffer.
         *
 * Returns:
 *  0   if decoding was OK
 *  1   if EOF reached
 *  -1  error
         */
int FFMPEG_Transcoder::process_single_fr() {
    int finished = 0;
    int ret = 0;

    if (m_out.m_nAudio_stream_idx > -1)
    {
        /** Use the encoder's desired frame size for processing. */
        const int output_frame_size = m_out.m_pAudio_codec_ctx->frame_size;

        /**
         * Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples.
         */

        while (av_audio_fifo_size(m_pAudioFifo) < output_frame_size) {
            /**
             * Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer.
             */

            if (read_decode_convert_and_store(&finished))
            {
                goto cleanup;
            }

            /**
             * If we are at the end of the input file, we continue
             * encoding the remaining audio samples to the output file.
             */
            if (finished)
            {
                break;
            }
        }

        /**
         * If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder.
         */
        while (av_audio_fifo_size(m_pAudioFifo) >= output_frame_size || (finished && av_audio_fifo_size(m_pAudioFifo) > 0))
        {
            /**
             * Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file.
             */
            if (load_encode_and_write())
            {
                goto cleanup;
            }
        }

        /**
         * If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish.
         */
        if (finished)
        {
            if (m_out.m_pAudio_codec_ctx != NULL && m_out.m_pAudio_codec_ctx->codec->capabilities & AV_CODEC_CAP_DELAY)
            {
                int data_written = 0;
                /** Flush the encoder as it may have delayed frames. */
                do
                {
                    if (encode_audio_frame(NULL, &data_written))
                    {
                        goto cleanup;
                    }
                } while (data_written);
            }

            ret = 1;
        }
    }
    else
    {
        if (read_decode_convert_and_store(&finished))
        {
            goto cleanup;
        }

        if (finished) {
            ret = 1;
        }
    }

    while (!m_VideoFifo.empty())
    {
        AVFrame *output_frame = m_VideoFifo.front();

        m_VideoFifo.pop();

        /** Encode one video frame. */
        int data_written = 0;
        output_frame->key_frame = 0;    // Leave that decision to decoder
        if (encode_video_frame(output_frame, &data_written))
        {
            av_frame_free(&output_frame);
            goto cleanup;
        }
        av_frame_free(&output_frame);
    }

    /**
         * If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish.
         */
    if (finished)
    {
        if (m_out.m_pVideo_codec_ctx != NULL && m_out.m_pVideo_codec_ctx->codec->capabilities & AV_CODEC_CAP_DELAY)
        {
            /** Flush the encoder as it may have delayed frames. */
            int data_written = 0;
            do
            {
                if (encode_video_frame(NULL, &data_written))
                {
                    goto cleanup;
                }
            } while (data_written);
        }

        ret = 1;
    }

    return ret;

cleanup:
    return -1;
}

/*
 * Properly calculate final file size. This is the sum of the size of
 * ID3v2, ID3v1, and raw MP3 data. This is theoretically only approximate
 * but in practice gives excellent answers, usually exactly correct.
 * Cast to 64-bit int to avoid overflow.
 */
size_t FFMPEG_Transcoder::calculate_size() {

    if (m_nCalculated_size == 0 && m_in.m_pFormat_ctx != NULL)
    {
        // TODO: check mp3 prediction. AAC/H264 OK now.
        double duration = ffmpeg_cvttime(m_in.m_pFormat_ctx->duration, AV_TIME_BASE_Q);
        size_t size = 0;

        if (m_in.m_nAudio_stream_idx > -1)
        {
            AVCodecID audio_codec_id = AV_CODEC_ID_AAC; // ??? TODO: aus der Kommandozeile...
            int64_t audiobitrate = get_output_bit_rate(m_in.m_pAudio_stream, params.audiobitrate);

            switch (audio_codec_id)
            {
            case AV_CODEC_ID_AAC:
            {
                // Try to predict the size of the AAC stream (this is fairly accurate, sometimes a bit larger, sometimes a bit too small
                size += (size_t)(duration * 1.025 * (double)audiobitrate / 8); // add 2.5% for overhead
                break;
            }
            case AV_CODEC_ID_MP3:
            {
                // TODO: calculate correct size of mp3
                // Kbps = bits per second / 8 = Bytes per second x 60 seconds = Bytes per minute x 60 minutes = Bytes per hour
                size += (size_t)(duration * (double)audiobitrate / 8) + ID3V1_TAG_LENGTH;

                //               You can calculate the size using the following formula:
                //               x = length of song in seconds
                //               y = bitrate in kilobits per second
                //               (x * y) / 8
                //               We divide by 8 to get the result in bytes.
                //               So for example if you have a 3 minute song
                //               3 minutes = 180 seconds
                //               128kbps * 180 seconds = 23,040 kilobits of data 23,040 kilobits / 8 = 2880 kb
                //               You would then convert to Megabytes by dividing by 1024:
                //               2880/1024 = 2.8125 Mb
                //               If all of this was done at a different encoding rate, say 192kbps it would look like this:
                //               (192 * 180) / 8 = 4320 kb / 1024 = 4.21875 Mb

                // Copied from lame
                //#define MAX_VBR_FRAME_SIZE 2880
                //
                //size_t Mp3Encoder::calculate_size() const {
                //    if (actual_size != 0) {
                //        return actual_size;
                //    } else if (params.vbr) {
                //        return id3size + ID3V1_TAG_LENGTH + MAX_VBR_FRAME_SIZE + (uint64_t)lame_get_totalframes(lame_encoder) * 144 * params.bitrate * 10 / (lame_get_in_samplerate(lame_encoder) / 100);
                //    } else {
                //        return id3size + ID3V1_TAG_LENGTH +                      (uint64_t)lame_get_totalframes(lame_encoder) * 144 * params.bitrate * 10 / (lame_get_out_samplerate(lame_encoder) / 100);
                //    }
                break;
            }
            default:
            {
                mp3fs_error("Internal error - unsupported audio codec '%s' for '%s'.", get_codec_name(audio_codec_id), m_in.m_pFormat_ctx->filename);
                break;
            }
            }
        }

        if (m_in.m_nVideo_stream_idx > -1)
        {
            if (m_bIsVideo)
            {
                AVCodecID video_codec_id = AV_CODEC_ID_H264; // ??? TODO: aus der Kommandozeile...
                int64_t videobitrate = get_output_bit_rate(m_in.m_pVideo_stream, params.videobitrate);
                int64_t bitrateoverhead = 0;

                videobitrate += bitrateoverhead;

                switch (video_codec_id)
                {
                case AV_CODEC_ID_H264:
                {
                    size += (size_t)(duration * 1.025  * (double)videobitrate / 8); // add 2.5% for overhead
                    break;
                }
                default:
                {
                    mp3fs_error("Internal error - unsupported video codec '%s' for '%s'.", get_codec_name(video_codec_id), m_in.m_pFormat_ctx->filename);
                    break;
                }
                }
            }
            else      // TODO: Add picture size
            {

            }
        }

        m_nCalculated_size = size;
    }

    return m_nCalculated_size;
}

/*
 * Encode any remaining PCM data in LAME internal buffers to the given
 * Buffer. This should be called after all input data has already been
 * passed to encode_pcm_data().
 */
int FFMPEG_Transcoder::encode_finish() {

    int ret = 0;

    /** Write the trailer of the output file container. */
    ret = write_output_file_trailer();
    if (ret < 0)
    {
        mp3fs_error("Error writing trailer (error '%s') for '%s'.", ffmpeg_geterror(ret).c_str(), m_in.m_pFormat_ctx->filename);
        ret = -1;
    }

    return 1;
}

const ID3v1 * FFMPEG_Transcoder::id3v1tag() const
{
    return &m_out.m_id3v1;
}

int FFMPEG_Transcoder::writePacket(void * pOpaque, unsigned char * pBuffer, int nBufSize)
{
    Buffer * buffer = (Buffer *)pOpaque;

    if (buffer == NULL)
    {
        return -1;
    }

    return (int)buffer->write((const uint8_t*)pBuffer, nBufSize);
}

int64_t FFMPEG_Transcoder::seek(void * pOpaque, int64_t i4Offset, int nWhence)
{
    Buffer * buffer = (Buffer *)pOpaque;
    int64_t i64ResOffset = 0;

    if (buffer != NULL)
    {
        if (nWhence & AVSEEK_SIZE)
        {
            i64ResOffset = buffer->tell();
        }

        else
        {
            nWhence &= ~(AVSEEK_SIZE | AVSEEK_FORCE);

            switch (nWhence)
            {
            case SEEK_CUR:
            {
                i4Offset = buffer->tell() + i4Offset;
                break;
            }

            case SEEK_END:
            {
                i4Offset = buffer->size() - i4Offset;
                break;
            }

            case SEEK_SET:  // SEEK_SET only supported
            {
                break;
            }
            }

            if (i4Offset < 0)
            {
                i4Offset = 0;
            }

            if (buffer->seek(i4Offset))
            {
                i64ResOffset = i4Offset;
            }

            else
            {
                i64ResOffset = 0;
            }
        }
    }

    return i64ResOffset;
}

/* Close the open FFmpeg file
 */
void FFMPEG_Transcoder::close()
{
    if (m_pAudioFifo)
    {
        av_audio_fifo_free(m_pAudioFifo);
        m_pAudioFifo = NULL;
    }

    while (m_VideoFifo.size())
    {
        AVFrame *output_frame = m_VideoFifo.front();
        m_VideoFifo.pop();

        av_frame_free(&output_frame);
    }

    if (m_pAudio_resample_ctx)
    {
        avresample_close(m_pAudio_resample_ctx);
        avresample_free(&m_pAudio_resample_ctx);
        m_pAudio_resample_ctx = NULL;
    }

    if (m_pSws_ctx != NULL)
    {
        sws_freeContext(m_pSws_ctx);
        m_pSws_ctx = NULL;
    }

    // Close output file
#if (AV_VERSION_MAJOR < 57)
    if (m_out.m_pAudio_codec_ctx)
    {
        avcodec_close(m_out.m_pAudio_codec_ctx);
        m_out.m_pAudio_codec_ctx = NULL;
    }

    if (m_out.m_pVideo_codec_ctx)
    {
        avcodec_close(m_out.m_pVideo_codec_ctx);
        m_out.m_pVideo_codec_ctx = NULL;
    }
#else
    if (m_out.m_pAudio_codec_ctx)
    {
        avcodec_free_context(&m_out.m_pAudio_codec_ctx);
        m_out.m_pAudio_codec_ctx = NULL;
    }

    if (m_out.m_pVideo_codec_ctx)
    {
        avcodec_free_context(&m_out.m_pVideo_codec_ctx);
        m_out.m_pVideo_codec_ctx = NULL;
    }
#endif

    if (m_out.m_pFormat_ctx != NULL)
    {
        AVIOContext *output_io_context  = (AVIOContext *)m_out.m_pFormat_ctx->pb;

#if (AV_VERSION_MAJOR >= 57)
        if (output_io_context != NULL)
        {
            av_freep(&output_io_context->buffer);
        }
#endif
        //        if (!(m_out.m_pFormat_ctx->oformat->flags & AVFMT_NOFILE))
        {
            av_freep(&output_io_context);
        }

        avformat_free_context(m_out.m_pFormat_ctx);
        m_out.m_pFormat_ctx = NULL;
    }

    // Close input file
#if (AV_VERSION_MAJOR < 57)
    if (m_in.m_pAudio_codec_ctx)
    {
        avcodec_close(m_in.m_pAudio_codec_ctx);
        m_in.m_pAudio_codec_ctx = NULL;
    }

    if (m_in.m_pVideo_codec_ctx)
    {
        avcodec_close(m_in.m_pVideo_codec_ctx);
        m_in.m_pVideo_codec_ctx = NULL;
    }
#else
    if (m_in.m_pAudio_codec_ctx)
    {
        avcodec_free_context(&m_in.m_pAudio_codec_ctx);
        m_in.m_pAudio_codec_ctx = NULL;
    }

    if (m_in.m_pVideo_codec_ctx)
    {
        avcodec_free_context(&m_in.m_pVideo_codec_ctx);
        m_in.m_pVideo_codec_ctx = NULL;
    }
#endif

    //if (m_in.m_pAudio_codec_ctx)
    if (m_in.m_pFormat_ctx != NULL)
    {
        avformat_close_input(&m_in.m_pFormat_ctx);
        m_in.m_pFormat_ctx = NULL;
    }

    mp3fs_debug("FFmpeg trancoder: closed.");
}

#pragma GCC diagnostic pop
