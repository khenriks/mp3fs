/*
 * FFMPEG decoder class header for mp3fs
 *
 * Copyright (C) 2017 Norbert Schlia (nschlia@oblivon-software.de)
 * Copyright (C) 2015 Thomas Schwarzenberger
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

#ifndef FFMPEG_TRANSCODER_H
#define FFMPEG_TRANSCODER_H

#pragma once

#include "ffmpeg_utils.h"

#include <queue>

class Buffer;

struct ID3v1
{
    char m_sTAG[3];         // Contains "TAG"
    char m_sSongTitle[30];  // Title of sound track
    char m_sSongArtist[30]; // Artist Name
    char m_sAlbumName[30];  // Album Name
    char m_sYear[4];        // Year of publishing
    char m_sComment[28];    // Any user comments
    char m_bPad;            // Must be '\0'
    char m_bTitleNo;
    char m_bGenre;          // Type of music
};

#define ID3V1_TAG_LENGTH sizeof(ID3v1)  // 128 bytes

typedef enum _tagOUTPUTTYPE
{
    TYPE_UNKNOWN,
    TYPE_MP3,
    TYPE_MP4,
    TYPE_ISMV
} OUTPUTTYPE;

class FFMPEG_Transcoder {
public:
    FFMPEG_Transcoder();
    ~FFMPEG_Transcoder();
    int open_file(const char* filename);
    int open_out_file(Buffer* buffer, const char* type);
    int process_single_fr();
    int encode_finish();

    time_t mtime() const;
    size_t calculate_size();

    const ID3v1 * id3v1tag() const;
    
protected:
    bool is_video() const;
    int open_codec_context(int *stream_idx, AVCodecContext **avctx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename);
    int add_stream(AVCodecID codec_id);
    int open_output_file(Buffer *buffer, const char* type);
    void copy_metadata(AVDictionary *metadata, AVStream *stream, bool bIsVideo);
    int process_metadata();
    void init_packet(AVPacket *packet);
    int init_input_frame(AVFrame **frame);
    int init_resampler();
    int init_fifo();
    int write_output_file_header();
    int decode_frame(AVPacket *input_packet, int *data_present);
    int init_converted_samples(uint8_t ***converted_input_samples, int frame_size);
    int convert_samples(uint8_t **input_data, uint8_t **converted_data, const int frame_size);
    int add_samples_to_fifo(uint8_t **converted_input_samples, const int frame_size);
    int read_decode_convert_and_store(int *finished);
    int init_audio_output_frame(AVFrame **frame, int frame_size);
    AVFrame *alloc_picture(AVPixelFormat pix_fmt, int width, int height);
    void produce_dts(AVPacket * pkt, int64_t *pts);
    int encode_audio_frame(AVFrame *frame, int *data_present);
    int encode_video_frame(AVFrame *frame, int *data_present);
    int load_encode_and_write();
    int write_output_file_trailer();

    static int writePacket(void * pOpaque, unsigned char * pBuffer, int nBufSize);
    static int64_t seek(void * pOpaque, int64_t i4Offset, int nWhence);

    int64_t get_output_bit_rate(AVStream *in_stream, int64_t bit_rate) const;
    
private:
    time_t                      m_mtime;
    size_t                      m_nCalculated_size;         // Use this as the size instead of computing it.
    bool                        m_bIsVideo;

    // Audio conversion and buffering
#ifdef _USE_LIBSWRESAMPLE
    SwrContext *                m_pSwr_ctx;
#else
    AVAudioResampleContext *    m_pAudio_resample_ctx;
#endif	
    AVAudioFifo *               m_pAudioFifo;
	
    // Video conversion and buffering
    SwsContext *                m_pSws_ctx;
    std::queue<AVFrame*>        m_VideoFifo;
    int64_t                     m_pts;
    int64_t                     m_pos;

    // Input file
    struct _tagINPUTFILE
    {
        AVFormatContext *       m_pFormat_ctx;
        AVCodecContext *        m_pAudio_codec_ctx;
        AVCodecContext *        m_pVideo_codec_ctx;
        AVStream *              m_pAudio_stream;
        AVStream *              m_pVideo_stream;
        int                     m_nAudio_stream_idx;
        int                     m_nVideo_stream_idx;
    } m_in;

    // Output file
    struct _tagOUTPUTFILE
    {
        OUTPUTTYPE              m_output_type;

        AVFormatContext *       m_pFormat_ctx;
        AVCodecContext *        m_pAudio_codec_ctx;
        AVCodecContext *        m_pVideo_codec_ctx;
        AVStream *              m_pAudio_stream;
        AVStream *              m_pVideo_stream;
        int                     m_nAudio_stream_idx;
        int                     m_nVideo_stream_idx;

        int64_t                 m_nAudio_pts;           // Global timestamp for the audio frames
        int64_t                 m_nVideo_pts;           // Global timestamp for the video frames

        int64_t                 m_nVideo_offset;

        ID3v1                   m_id3v1;
    } m_out;
};

#endif
