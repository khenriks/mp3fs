/*
 * FFMPEG decoder class source for mp3fs
 *
 * Copyright (C) 2015 Thomas Schwarzenberger
 * FFMPEG supplementals by Norbert Schlia (nschlia@oblivon-software.de)
 *
 * Transcoder derived from this example:
 * https://fossies.org/linux/libav/doc/examples/transcode_aac.c
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

#include <vector>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "transcode.h"
#include "buffer.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

FfmpegTranscoder::FfmpegTranscoder()
    : m_nActual_size(0)
    , m_pInput_format_context(NULL)
    , m_pAudio_dec_ctx(NULL)
    , m_pVideo_dec_ctx(NULL)
    , m_pAudio_stream(NULL)
    , m_pVideo_stream(NULL)
    , m_nAudio_stream_idx(0)
    , m_nVideo_stream_idx(0)
    , m_pOutput_format_context(NULL)
    , m_pOutput_codec_context(NULL)
    , m_pts(0)
    , m_eOutputType(TYPE_UNKNOWN)
    , m_pResample_context(NULL)
    , m_pFifo(NULL)
{
    mp3fs_debug("FFMPEG trancoder: ready to initialise.");

    // Initialise ID3v1.1 tag structure
    memset(&m_id3v1, ' ', sizeof(ID3v1));
    memcpy(&m_id3v1.szTAG, "TAG", 3);
    m_id3v1.cPad = '\0';
    m_id3v1.cTitleNo = 0;
    m_id3v1.cGenre = 0;
}

/* Free the FFMPEG en/decoder and close the open FFMPEG file
 * after the transcoding process has finished.
 */
FfmpegTranscoder::~FfmpegTranscoder() {

    if (m_pFifo)
    {
        av_audio_fifo_free(m_pFifo);
    }
    if (m_pResample_context) {
        avresample_close(m_pResample_context);
        avresample_free(&m_pResample_context);
    }
    if (m_pOutput_codec_context)
    {
        avcodec_free_context(&m_pOutput_codec_context);
    }
    if (m_pOutput_format_context) {

        AVIOContext *output_io_context  = (AVIOContext *)m_pOutput_format_context->pb;

        //avio_close(output_format_context->pb);

        av_freep(&output_io_context->buffer);
        av_freep(&output_io_context);

        avformat_free_context(m_pOutput_format_context);
    }
    if (m_pAudio_dec_ctx)
    {
        avcodec_free_context(&m_pAudio_dec_ctx);
    }
    if (m_pInput_format_context)
    {
        avformat_close_input(&m_pInput_format_context);
    }
    mp3fs_debug("FFMPEG trancoder: closed.");
}

/*
 * Open codec context for desired media type
 */
int FfmpegTranscoder::open_codec_context(int *stream_idx, AVCodecContext **avctx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename)
{
    int ret;
    int stream_index;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        mp3fs_error("FFMPEG transcoder: could not find %s stream in input file '%s'", get_media_type_string(type), filename);
        return ret;
    } else {
        AVStream *st;

        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* Init the decoders, with or without reference counting */
        // av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);

#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )
        /** Find a decoder for the audio stream. */
        if (!(dec = avcodec_find_decoder(st->codecpar->codec_id))) {
            mp3fs_error("FFMPEG transcoder: Could not find input codec");
            return AVERROR_EXIT;
        }

        /** allocate a new decoding context */
        dec_ctx = avcodec_alloc_context3(dec);
        if (!dec_ctx) {
            mp3fs_error("FFMPEG transcoder: Could not allocate a decoding context");
            //            avformat_close_input(&fmt_ctx);
            return AVERROR(ENOMEM);
        }

        /** initialize the stream parameters with demuxer information */
        ret = avcodec_parameters_to_context(dec_ctx, st->codecpar);
        if (ret < 0) {
            return ret;
        }

#else
        dec_ctx = st->codec;

        /* find decoder for the stream */
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            mp3fs_error("FFMPEG transco"
                        "der: failed to find %s codec", get_media_type_string(type));
            return AVERROR(EINVAL);
        }
#endif
        ret = avcodec_open2(dec_ctx, dec, &opts);
        if (ret < 0) {
            mp3fs_error("FFMPEG transcoder: failed to find %s codec (error '%s')", get_media_type_string(type), ffmpeg_geterror(ret).c_str());
            return ret;
        }

        *stream_idx = stream_index;

        *avctx = dec_ctx;
    }

    return 0;
}

/*
 * Open the given FFMPEG file and prepare for decoding. After this function,
 * the other methods can be used to process the file.
 */
int FfmpegTranscoder::open_file(const char* filename) {
    AVDictionary * pDictionaryOptions = NULL;
    int ret;

    mp3fs_debug("FFMPEG transcoder: initialising.");

    struct stat s;
    if (stat(filename, &s) < 0) {
        mp3fs_error("FFMPEG transcoder: stat failed.");
        return -1;
    }
    m_mtime = s.st_mtime;

    //    This allows selecting if the demuxer should consider all streams to be
    //    found after the first PMT and add further streams during decoding or if it rather
    //    should scan all that are within the analyze-duration and other limits

    ret = ::av_dict_set(&pDictionaryOptions, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    if (ret < 0)
    {
        mp3fs_error("FFMPEG transcoder: error setting dictionary options: %s", ffmpeg_geterror(ret).c_str());
        return -1; // Couldn't open file
    }

    /** Open the input file to read from it. */
    assert(m_pInput_format_context == NULL);
    ret = avformat_open_input(&m_pInput_format_context, filename, NULL, &pDictionaryOptions);
    if (ret < 0) {
        mp3fs_error("FFMPEG transcoder: Could not open input file '%s' (error '%s')",
                    filename, ffmpeg_geterror(ret).c_str());
        //input_format_context = NULL;
        return ret;
    }

    ret = ::av_dict_set(&pDictionaryOptions, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    if (ret < 0)
    {
        mp3fs_error("FFMPEG transcoder: error setting dictionary options: %s", ffmpeg_geterror(ret).c_str());
        return -1; // Couldn't open file
    }

    AVDictionaryEntry * t;
    if ((t = av_dict_get(pDictionaryOptions, "", NULL, AV_DICT_IGNORE_SUFFIX)))
    {
        mp3fs_error("FFMPEG transcoder: option %s not found.", t->key);
        return -1; // Couldn't open file
    }

#if (LIBAVFORMAT_VERSION_MICRO >= 100 && LIBAVFORMAT_VERSION_INT >= LIBAVCODEC_MIN_VERSION_INT )
    av_format_inject_global_side_data(m_pInput_format_context);
#endif

    /** Get information on the input file (number of streams etc.). */
    ret = avformat_find_stream_info(m_pInput_format_context, NULL);
    if (ret < 0) {
        mp3fs_error("FFMPEG transcoder: Could not open find stream info (error '%s')",
                    ffmpeg_geterror(ret).c_str());
        avformat_close_input(&m_pInput_format_context);
        m_pInput_format_context = NULL;
        return ret;
    }

#if 0
    // WILL ADD VIDEO SUPPORT LATER...
    // Open best match video codec
    // Save the video decoder context for easier access later.
    if (open_codec_context(&m_nVideo_stream_idx, &m_pVideo_dec_ctx, m_pInput_format_context, AVMEDIA_TYPE_VIDEO, filename) >= 0) {
        m_pVideo_stream = m_pInput_format_context->streams[m_nVideo_stream_idx];

        /* allocate image where the decoded image will be put */
        ret = av_image_alloc(m_pVideo_dst_data, m_nVideo_dst_linesize,
                             m_pVideo_dec_ctx->width, m_pVideo_dec_ctx->height,
                             m_pVideo_dec_ctx->pix_fmt, 1);
        if (ret < 0) {
            mp3fs_error("FFMPEG transcoder: could not allocate raw video buffer: %s", ffmpeg_geterror(ret).c_str());
            return -1;
        }
        m_nVideo_dst_bufsize = ret;
    }
#endif

    // Open best match audio codec
    // Save the audio decoder context for easier access later.
    if (open_codec_context(&m_nAudio_stream_idx, &m_pAudio_dec_ctx, m_pInput_format_context, AVMEDIA_TYPE_AUDIO, filename) >= 0) {
        m_pAudio_stream = m_pInput_format_context->streams[m_nAudio_stream_idx];
    }

    //m_pInput_codec_context = avctx;

    return 0;
}

int FfmpegTranscoder::open_out_file(Buffer *buffer, const char* type) {

    /** Open the output file for writing. */
    if (open_output_file(buffer, type)) {
        return -1;
    }
    /** Initialize the resampler to be able to convert audio sample formats. */
    if (init_resampler()){
        return -1;
    }
    /** Initialize the FIFO buffer to store audio samples to be encoded. */
    if (init_fifo()){
        return -1;
    }
    /** Write the header of the output file container. */
    if (write_output_file_header()){
        return -1;
    }

    return 0;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 */
int FfmpegTranscoder::open_output_file(Buffer *buffer, const char* type)
{
    AVCodecContext *avctx           = NULL;
    AVIOContext *output_io_context  = NULL;
    AVStream *stream                = NULL;
    AVCodec *output_codec           = NULL;
    AVDictionary * pOptionsDict     = NULL;
    const char *ext;
    AVCodecID codecid;
    int error = 0;

    if (!strcasecmp(type, "mp3"))
    {
        ext = "mp3";
        codecid = AV_CODEC_ID_MP3;
        m_eOutputType = TYPE_MP3;
    }
    else if (!strcasecmp(type, "mp4"))
    {
        //  ext = "ISMV";
        ext = "mp4";
        codecid = AV_CODEC_ID_AAC;
        m_eOutputType = TYPE_MP4;
    }
    else if (!strcasecmp(type, "ismv"))
    {
        ext = "ISMV";
        codecid = AV_CODEC_ID_AAC;
        m_eOutputType = TYPE_ISMV;
    }
    else
    {
        mp3fs_error("FFMPEG transcoder: Unknown format type \"%s\"", type);
        return -1;
    }

    mp3fs_debug("FFMPEG transcoder: Opening format type \"%s\"", type);

    //    /** Open the output file to write to it. */
    //    if ((error = avio_open(&output_io_context, filename,
    //                           AVIO_FLAG_WRITE)) < 0) {
    //        mp3fs_error("FFMPEG transcoder: Could not open output file '%s' (error '%s')",
    //                    filename, ffmpeg_geterror(error).c_str());
    //        return error;
    //    }

    /** Create a new format context for the output container format. */
    if (!(m_pOutput_format_context = avformat_alloc_context())) {
        mp3fs_error("FFMPEG transcoder: Could not allocate output format context");
        return AVERROR(ENOMEM);
    }

    int nBufSize = 1024;
    output_io_context = ::avio_alloc_context(
                (unsigned char *) ::av_malloc(nBufSize + FF_INPUT_BUFFER_PADDING_SIZE),
                nBufSize,
                1,
                (void *)buffer,
                NULL /*readPacket*/,
                writePacket,
                seek);
    //                NULL);

    //        output_format_context->max_analyze_duration2 = 32; //1 * AV_TIME_BASE;
    //        output_format_context->probesize2 = 32;
    m_pOutput_format_context->flags |= AVFMT_FLAG_IGNIDX;

    /** Associate the output file (pointer) with the container format context. */
    m_pOutput_format_context->pb = output_io_context;

    //    error = avformat_open_input(output_format_context, NULL, NULL, NULL);
    //    if (error < 0) {
    //        mp3fs_error("FFMPEG transcoder: Could not open output context (error '%s')",
    //                    ffmpeg_geterror(error).c_str());
    //        return error;
    //    }

    /** Guess the desired container format based on the file extension. */
    if (!(m_pOutput_format_context->oformat = av_guess_format(ext, NULL /*filename*/, NULL))) {
        mp3fs_error("FFMPEG transcoder: Could not find output file format");
        goto cleanup;
    }

    //    av_strlcpy(m_pOutput_format_context->filename, filename,
    //               sizeof(m_pOutput_format_context->filename));

    /** Find the encoder to be used by its name. */
    if (!(output_codec = avcodec_find_encoder(codecid))) {
        mp3fs_error("FFMPEG transcoder: Could not find the encoder.");
        goto cleanup;
    }

    /** Create a new audio stream in the output file container. */
    if (!(stream = avformat_new_stream(m_pOutput_format_context, NULL))) {
        mp3fs_error("FFMPEG transcoder: Could not create new stream");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )
    avctx = avcodec_alloc_context3(output_codec);
    if (!avctx) {
        mp3fs_error("FFMPEG transcoder: Could not allocate an encoding context");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }
#else
    avctx = stream->codec;
#endif

    /**
     * Set the basic encoder parameters.
     * The input file's sample rate is used to avoid a sample rate conversion.
     */
    avctx->channels       = 2;
    avctx->channel_layout = av_get_default_channel_layout(avctx->channels);
    avctx->sample_rate    = m_pAudio_dec_ctx->sample_rate;
    avctx->sample_fmt     = output_codec->sample_fmts[0];
    avctx->bit_rate       = params.bitrate * 1000;

    /** Allow the use of the experimental AAC encoder */
    avctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    /** Set the sample rate for the container. */
    stream->time_base.den = m_pAudio_dec_ctx->sample_rate;
    stream->time_base.num = 1;

#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )
    /**
     * Some container formats (like MP4) require global headers to be present
     * Mark the encoder so that it behaves accordingly.
     */
    if (m_pOutput_format_context->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
#endif

    if (!::av_dict_get(pOptionsDict, "threads", NULL, 0))
    {
        ::av_dict_set(&pOptionsDict, "threads", "auto", 0);
    }

    // set -strict -2 for aac
    ::av_dict_set(&pOptionsDict, "strict", "-2", 0);

    /** Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, output_codec, &pOptionsDict)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could not open output codec (error '%s')",
                    ffmpeg_geterror(error).c_str());
        goto cleanup;
    }

#if (LIBAVCODEC_VERSION_MICRO >= 100 && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 64, 101 ) )
    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        mp3fs_error("FFMPEG transcoder: Could not initialize stream parameters");
        goto cleanup;
    }
#endif

    /** Save the encoder context for easier access later. */
    m_pOutput_codec_context = avctx;

    return 0;

cleanup:
    avcodec_free_context(&avctx);
    //avio_close(m_pOutput_format_context->pb);
    av_freep(&m_pOutput_format_context->pb);
    avformat_free_context(m_pOutput_format_context);
    m_pOutput_format_context = NULL;
    return error < 0 ? error : AVERROR_EXIT;
}

/** Initialize one data packet for reading or writing. */
void FfmpegTranscoder::init_packet(AVPacket *packet)
{
    av_init_packet(packet);
    /** Set the packet data and size so that it is recognized as being empty. */
    packet->data = NULL;
    packet->size = 0;
}

/** Initialize one audio frame for reading from the input file */
int FfmpegTranscoder::init_input_frame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        mp3fs_error("FFMPEG transcoder: Could not allocate input frame");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libavresample takes care of this, but requires initialization.
 */
int FfmpegTranscoder::init_resampler()
{
    /**
     * Only initialize the resampler if it is necessary, i.e.,
     * if and only if the sample formats differ.
     */
    if (m_pAudio_dec_ctx->sample_fmt != m_pOutput_codec_context->sample_fmt ||
            m_pAudio_dec_ctx->channels != m_pOutput_codec_context->channels) {
        int error;

        /** Create a resampler context for the conversion. */
        if (!(m_pResample_context = avresample_alloc_context())) {
            mp3fs_error("FFMPEG transcoder: Could not allocate resample context");
            return AVERROR(ENOMEM);
        }

        /**
         * Set the conversion parameters.
         * Default channel layouts based on the number of channels
         * are assumed for simplicity (they are sometimes not detected
         * properly by the demuxer and/or decoder).
         */
        av_opt_set_int(m_pResample_context, "in_channel_layout",
                       av_get_default_channel_layout(m_pAudio_dec_ctx->channels), 0);
        av_opt_set_int(m_pResample_context, "out_channel_layout",
                       av_get_default_channel_layout(m_pOutput_codec_context->channels), 0);
        av_opt_set_int(m_pResample_context, "in_sample_rate",
                       m_pAudio_dec_ctx->sample_rate, 0);
        av_opt_set_int(m_pResample_context, "out_sample_rate",
                       m_pOutput_codec_context->sample_rate, 0);
        av_opt_set_int(m_pResample_context, "in_sample_fmt",
                       m_pAudio_dec_ctx->sample_fmt, 0);
        av_opt_set_int(m_pResample_context, "out_sample_fmt",
                       m_pOutput_codec_context->sample_fmt, 0);

        /** Open the resampler with the specified parameters. */
        if ((error = avresample_open(m_pResample_context)) < 0) {
            mp3fs_error("FFMPEG transcoder: Could not open resample context");
            avresample_free(&m_pResample_context);
            m_pResample_context = NULL;
            return error;
        }
    }
    return 0;
}

/** Initialize a FIFO buffer for the audio samples to be encoded. */
int FfmpegTranscoder::init_fifo()
{
    /** Create the FIFO buffer based on the specified output sample format. */
    if (!(m_pFifo = av_audio_fifo_alloc(m_pOutput_codec_context->sample_fmt, m_pOutput_codec_context->channels, 1))) {
        mp3fs_error("FFMPEG transcoder: Could not allocate FIFO");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/** Write the header of the output file container. */
int FfmpegTranscoder::write_output_file_header()
{
    int error;
    AVDictionary* dict = nullptr;

    if (m_pOutput_codec_context->codec_id == AV_CODEC_ID_AAC) {
        // Settings for fast playback start in HTML5
        av_dict_set(&dict, "movflags", "faststart", 0);
        av_dict_set(&dict, "movflags", "empty_moov", 0);
        av_dict_set(&dict, "frag_duration", "10000000", 0);

#if 0
        // Geht (empty_moov+empty_moov automatisch mit isml)
        //av_dict_set(&dict, "movflags", "isml+frag_keyframe+separate_moof", 0);
        av_dict_set(&dict, "movflags", "isml", 0);
        // spielt nicht in FF (spielt 10 Sekunden...)
        av_dict_set(&dict, "frag_duration", "10000000", 0);
#endif
        // spielt 50 Sekunden...
        // ISMV NICHT av_dict_set(&dict, "frag_duration", "50000000", 0);

        av_dict_set(&dict, "flags:a", "+global_header", 0);
        av_dict_set(&dict, "flags:v", "+global_header", 0);
        av_dict_set(&dict, "profile:v", "baseline", 0);
    }

    if ((error = avformat_write_header(m_pOutput_format_context, &dict)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could not write output file header (error '%s')", ffmpeg_geterror(error).c_str());
        return error;
    }
    return 0;
}

/** Decode one audio frame from the input file. */
int FfmpegTranscoder::decode_audio_frame(AVFrame *frame, int *data_present, int *finished)
{
    /** Packet used for temporary storage. */
    AVPacket input_packet;
    int error;
    init_packet(&input_packet);

    /** Read one audio frame from the input file into a temporary packet. */
    if ((error = av_read_frame(m_pInput_format_context, &input_packet)) < 0) {
        /** If we are the the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            mp3fs_error("FFMPEG transcoder: Could not read frame (error '%s')",
                        ffmpeg_geterror(error).c_str());
            return error;
        }
    }

    /**
     * Decode the audio frame stored in the temporary packet.
     * The input audio stream decoder is used to do this.
     * If we are at the end of the file, pass an empty packet to the decoder
     * to flush it.
     */
    error = avcodec_decode_audio4(m_pAudio_dec_ctx, frame,
                                  data_present, &input_packet);
    if (error < 0 && error != -1094995529) {
        mp3fs_error("FFMPEG transcoder: Could not decode frame (error '%s')",
                    ffmpeg_geterror(error).c_str());
        av_packet_unref(&input_packet);
        return error;
    }

    /**
     * If the decoder has not been flushed completely, we are not finished,
     * so that this function has to be called again.
     */
    if (*finished && *data_present)
        *finished = 0;
    av_packet_unref(&input_packet);
    return 0;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 */
int FfmpegTranscoder::init_converted_samples(uint8_t ***converted_input_samples, int frame_size)
{
    int error;

    /**
     * Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples = (uint8_t **)calloc(m_pOutput_codec_context->channels,
                                                        sizeof(**converted_input_samples)))) {
        mp3fs_error("FFMPEG transcoder: Could not allocate converted input sample pointers");
        return AVERROR(ENOMEM);
    }

    /**
     * Allocate memory for the samples of all channels in one consecutive
     * block for convenience.
     */
    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  m_pOutput_codec_context->channels,
                                  frame_size,
                                  m_pOutput_codec_context->sample_fmt, 0)) < 0) {
        mp3fs_error("Could not allocate converted input samples (error '%s')",
                    ffmpeg_geterror(error).c_str());
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return error;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is specified
 * by frame_size.
 */
int FfmpegTranscoder::convert_samples(uint8_t **input_data, uint8_t **converted_data, const int frame_size)
{
    if (m_pResample_context != NULL)
    {
        int error;

        /** Convert the samples using the resampler. */
        if ((error = avresample_convert(m_pResample_context, converted_data, 0,
                                        frame_size, input_data, 0, frame_size)) < 0) {
            mp3fs_error("FFMPEG transcoder: Could not convert input samples (error '%s')",
                        ffmpeg_geterror(error).c_str());
            return error;
        }

     /**
     * Perform a sanity check so that the number of converted samples is
     * not greater than the number of samples to be converted.
     * If the sample rates differ, this case has to be handled differently
     */
        if (avresample_available(m_pResample_context)) {
            mp3fs_error("FFMPEG transcoder: Converted samples left over");
            return AVERROR_EXIT;
        }
    }
    else
    {
        // No resampling, just copy samples
        if (!av_sample_fmt_is_planar(m_pAudio_dec_ctx->sample_fmt))
        {
            memcpy(converted_data[0], input_data[0], frame_size * av_get_bytes_per_sample(m_pOutput_codec_context->sample_fmt));
        }
        else
        {
            for (int n = 0; n < m_pAudio_dec_ctx->channels; n++)
            {
                memcpy(converted_data[n], input_data[n], frame_size * av_get_bytes_per_sample(m_pOutput_codec_context->sample_fmt));
            }
        }
    }
    return 0;
}

/** Add converted input audio samples to the FIFO buffer for later processing. */
int FfmpegTranscoder::add_samples_to_fifo(uint8_t **converted_input_samples, const int frame_size)
{
    int error;

    /**
     * Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples.
     */
    if ((error = av_audio_fifo_realloc(m_pFifo, av_audio_fifo_size(m_pFifo) + frame_size)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could not reallocate FIFO");
        return error;
    }

    /** Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(m_pFifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        mp3fs_error("FFMPEG transcoder: Could not write data to FIFO");
        return AVERROR_EXIT;
    }
    return 0;
}

/**
 * Read one audio frame from the input file, decodes, converts and stores
 * it in the FIFO buffer.
 */
int FfmpegTranscoder::read_decode_convert_and_store(int *finished)
{
    /** Temporary storage of the input samples of the frame read from the file. */
    AVFrame *input_frame = NULL;
    /** Temporary storage for the converted input samples. */
    uint8_t **converted_input_samples = NULL;
    int data_present;
    int ret = AVERROR_EXIT;

    /** Initialize temporary storage for one input frame. */
    if (init_input_frame(&input_frame))
        goto cleanup;
    /** Decode one frame worth of audio samples. */
    if (decode_audio_frame(input_frame, &data_present, finished))
        goto cleanup;
    /**
     * If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error.
     */
    if (*finished && !data_present) {
        ret = 0;
        goto cleanup;
    }
    /** If there is decoded data, convert and store it */
    if (data_present) {
        /** Initialize the temporary storage for the converted input samples. */
        if (init_converted_samples(&converted_input_samples, input_frame->nb_samples))
            goto cleanup;

        /**
         * Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples.
         */
        if (convert_samples(input_frame->extended_data, converted_input_samples,
                            input_frame->nb_samples))
            goto cleanup;

        /** Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(converted_input_samples, input_frame->nb_samples))
            goto cleanup;
        ret = 0;
    }
    ret = 0;

cleanup:
    if (converted_input_samples) {
        av_freep(&converted_input_samples[0]);
        free(converted_input_samples);
    }
    av_frame_free(&input_frame);

    return ret;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 */
int FfmpegTranscoder::init_output_frame(AVFrame **frame, int frame_size)
{
    int error;

    /** Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        mp3fs_error("FFMPEG transcoder: Could not allocate output frame");
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
    (*frame)->channel_layout = m_pOutput_codec_context->channel_layout;
    (*frame)->format         = m_pOutput_codec_context->sample_fmt;
    (*frame)->sample_rate    = m_pOutput_codec_context->sample_rate;

    /**
     * Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified.
     */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could allocate output frame samples (error '%s')",
                    ffmpeg_geterror(error).c_str());
        av_frame_free(frame);
        return error;
    }

    return 0;
}

/** Encode one frame worth of audio to the output file. */
int FfmpegTranscoder::encode_audio_frame(AVFrame *frame, int *data_present)
{
    /** Packet used for temporary storage. */
    AVPacket output_packet;
    int error;
    init_packet(&output_packet);

    /** Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = m_pts;
        m_pts += frame->nb_samples;
    }

    /**
     * Encode the audio frame and store it in the temporary packet.
     * The output audio stream encoder is used to do this.
     */
    if ((error = avcodec_encode_audio2(m_pOutput_codec_context, &output_packet,
                                       frame, data_present)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could not encode frame (error '%s')",
                    ffmpeg_geterror(error).c_str());
        av_packet_unref(&output_packet);
        return error;
    }

    /** Write one audio frame from the temporary packet to the output file. */
    if (*data_present) {
        if ((error = av_interleaved_write_frame /*av_write_frame*/(m_pOutput_format_context, &output_packet)) < 0) {
            mp3fs_error("FFMPEG transcoder: Could not write frame (error '%s')",
                        ffmpeg_geterror(error).c_str());
            av_packet_unref(&output_packet);
            return error;
        }

        av_packet_unref(&output_packet);
    }

    return 0;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 */
int FfmpegTranscoder::load_encode_and_write()
{
    /** Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
    /**
     * Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size
     */
    const int frame_size = FFMIN(av_audio_fifo_size(m_pFifo), m_pOutput_codec_context->frame_size);
    int data_written;

    /** Initialize temporary storage for one output frame. */
    if (init_output_frame(&output_frame, frame_size))
        return AVERROR_EXIT;

    /**
     * Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily.
     */
    if (av_audio_fifo_read(m_pFifo, (void **)output_frame->data, frame_size) < frame_size) {
        mp3fs_error("FFMPEG transcoder: Could not read data from FIFO");
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
int FfmpegTranscoder::write_output_file_trailer()
{
    int error;
    if ((error = av_write_trailer(m_pOutput_format_context)) < 0) {
        mp3fs_error("FFMPEG transcoder: Could not write output file trailer (error '%s')",
                    ffmpeg_geterror(error).c_str());
        return error;
    }
    return 0;
}

time_t FfmpegTranscoder::mtime() {
    return m_mtime;
}



/*
 * Process the metadata in the FFMPEG file. This should be called at the
 * beginning, before reading audio data. The set_text_tag() and
 * set_picture_tag() methods of the given Encoder will be used to set the
 * metadata, with results going into the given Buffer. This function will also
 * read the actual PCM stream parameters.
 */

#define tagcpy(dst, src)    \
    for (char *p1 = (dst), *pend = p1 + sizeof(dst), *p2 = (src); *p2 && p1 < pend; p1++, p2++) \
    *p1 = *p2;

int FfmpegTranscoder::process_metadata() {

    mp3fs_debug("FFMPEG transcoder: processing metadata");

    AVDictionaryEntry *tag = NULL;

    while ((tag = av_dict_get(m_pInput_format_context->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
    {
        av_dict_set(&m_pOutput_format_context->metadata, tag->key, tag->value, 0);

        if (m_eOutputType == TYPE_MP3)
        {
            if (!strcasecmp(tag->key, "ARTIST"))
            {
                tagcpy(m_id3v1.szSongArtist, tag->value);
            }
            else if (!strcasecmp(tag->key, "TITLE"))
            {
                tagcpy(m_id3v1.szSongTitle, tag->value);
            }
            else if (!strcasecmp(tag->key, "ALBUM"))
            {
                tagcpy(m_id3v1.szAlbumName, tag->value);
            }
            else if (!strcasecmp(tag->key, "COMMENT"))
            {
                tagcpy(m_id3v1.szComment, tag->value);
            }
            else if (!strcasecmp(tag->key, "YEAR") || !strcasecmp(tag->key, "DATE"))
            {
                tagcpy(m_id3v1.szYear, tag->value);
            }
            else if (!strcasecmp(tag->key, "TRACK"))
            {
                m_id3v1.cTitleNo = (char)atoi(tag->value);
            }
        }
    }

    // Pictures later. More complicated...

    return 0;
}

/*
 * Process a single frame of audio data. The encode_pcm_data() method
 * of the Encoder will be used to process the resulting audio data, with the
 * result going into the given Buffer.
 */
int FfmpegTranscoder::process_single_fr(Buffer* /*buffer*/) {
    int ret = 0;

    {
        /** Use the encoder's desired frame size for processing. */
        const int output_frame_size = m_pOutput_codec_context->frame_size;
        int finished                = 0;

        /**
         * Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples.
         */
        while (av_audio_fifo_size(m_pFifo) < output_frame_size) {
            /**
             * Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer.
             */
            if (read_decode_convert_and_store(&finished))
                goto cleanup;

            /**
             * If we are at the end of the input file, we continue
             * encoding the remaining audio samples to the output file.
             */
            if (finished)
                break;
        }

        /**
         * If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder.
         */
        while (av_audio_fifo_size(m_pFifo) >= output_frame_size ||
               (finished && av_audio_fifo_size(m_pFifo) > 0))
            /**
             * Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file.
             */
            if (load_encode_and_write())
                goto cleanup;

        /**
         * If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish.
         */
        if (finished) {
            int data_written;
            /** Flush the encoder as it may have delayed frames. */
            do {
                if (encode_audio_frame(NULL, &data_written))
                    goto cleanup;
            } while (data_written);
            ret = 1;
        }
    }

    return ret;
cleanup:
    return -1;
}

/*
 * Get the actual number of bytes in the encoded file, i.e. without any
 * padding. Valid only after encode_finish() has been called.
 */
size_t FfmpegTranscoder::get_actual_size() const {
    return m_nActual_size;
}

/*
 * Properly calculate final file size. This is the sum of the size of
 * ID3v2, ID3v1, and raw MP3 data. This is theoretically only approximate
 * but in practice gives excellent answers, usually exactly correct.
 * Cast to 64-bit int to avoid overflow.
 */
size_t FfmpegTranscoder::calculate_size() const {
    if (m_nActual_size != 0) {
        return m_nActual_size;
    } else if (m_pInput_format_context != NULL) {
        return (size_t)(1250 *ffmpeg_cvttime(m_pInput_format_context->duration, AV_TIME_BASE_Q) * params.bitrate / 8);    // 1.25 Magix number...
    } else {
        return 0;
    }
}

/*
 * Encode any remaining PCM data in LAME internal buffers to the given
 * Buffer. This should be called after all input data has already been
 * passed to encode_pcm_data().
 */
int FfmpegTranscoder::encode_finish(Buffer& buffer) {

    int ret = 0;

    /** Write the trailer of the output file container. */
    ret = write_output_file_trailer();
    if (ret < 0)
    {
        mp3fs_error("FFMPEG transcoder: error writing trailer: %s", ffmpeg_geterror(ret).c_str());
        ret = -1;
    }

    m_nActual_size = buffer.actual_size();

    return 1;
}

const ID3v1 * FfmpegTranscoder::id3v1tag() const
{
    return &m_id3v1;
}

int FfmpegTranscoder::writePacket(void * pOpaque, unsigned char * pBuffer, int nBufSize)
{
    Buffer * buffer = (Buffer *)pOpaque;

    if (buffer == NULL)
    {
        return -1;
    }

    return (int)buffer->write((const uint8_t*)pBuffer, nBufSize);
}

int64_t FfmpegTranscoder::seek(void * pOpaque, int64_t i4Offset, int nWhence)
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

#pragma GCC diagnostic pop
