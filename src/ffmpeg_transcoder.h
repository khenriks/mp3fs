/*
 * FFMPEG decoder class header for mp3fs
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

#ifndef FFMPEG_TRANSCODER_H
#define FFMPEG_TRANSCODER_H

#include "ffmpeg_utils.h"

class Buffer;

struct ID3v1
{
    char szTAG[3];          // Contains "TAG"
    char szSongTitle[30];   // Title of sound track
    char szSongArtist[30];  // Artist Name
    char szAlbumName[30];   // Album Name
    char szYear[4];         // Year of publishing
    char szComment[28];     // Any user comments
    char cPad;
    char cTitleNo;
    char cGenre;            // Type of music
};

#define id3v1_tag_length sizeof(ID3v1)  // 128 bytes

typedef enum _tagOUTPUTTYPE
{
    TYPE_UNKNOWN,
    TYPE_MP3,
    TYPE_MP4,
    TYPE_ISMV
} OUTPUTTYPE;

class FfmpegTranscoder {
public:
    FfmpegTranscoder();
    ~FfmpegTranscoder();
    int open_file(const char* filename);
    int open_out_file(Buffer* buffer, const char* type);
    time_t mtime();
    int process_metadata();
    int process_single_fr(Buffer* buffer);

    size_t get_actual_size() const;
    size_t calculate_size() const;

    int encode_finish(Buffer& buffer);

    const ID3v1 * id3v1tag() const;

protected:
    int open_codec_context(int *stream_idx, AVCodecContext **avctx, AVFormatContext *fmt_ctx, AVMediaType type, const char *filename);
    int open_output_file(Buffer *buffer, const char* type);
    void init_packet(AVPacket *packet);
    int init_input_frame(AVFrame **frame);
    int init_resampler();
    int init_fifo();
    int write_output_file_header();
    int decode_audio_frame(AVFrame *frame, int *data_present, int *finished);
    int init_converted_samples(uint8_t ***converted_input_samples, int frame_size);
    int convert_samples(uint8_t **input_data, uint8_t **converted_data, const int frame_size);
    int add_samples_to_fifo(uint8_t **converted_input_samples, const int frame_size);
    int read_decode_convert_and_store(int *finished);
    int init_output_frame(AVFrame **frame, int frame_size);
    int encode_audio_frame(AVFrame *frame, int *data_present);
    int load_encode_and_write();
    int write_output_file_trailer();

    static int writePacket(void * pOpaque, unsigned char * pBuffer, int nBufSize);
    static int64_t seek(void * pOpaque, int64_t i4Offset, int nWhence);

private:
    time_t                      m_mtime;
    size_t                      m_nActual_size;         // Use this as the size instead of computing it.

    // Input file
    AVFormatContext *           m_pInput_format_context;
    AVCodecContext *            m_pAudio_dec_ctx;
    AVCodecContext *            m_pVideo_dec_ctx;
    AVStream *                  m_pAudio_stream;
    AVStream *                  m_pVideo_stream;
    int                         m_nAudio_stream_idx;
    int                         m_nVideo_stream_idx;

    // Output file
    AVFormatContext *           m_pOutput_format_context;
    AVCodecContext *            m_pOutput_codec_context;

    //    uint8_t *                   m_pVideo_dst_data[4];
    //    int                         m_nVideo_dst_linesize[4];
    //    int                         m_nVideo_dst_bufsize;

    int64_t                     m_pts;                  // Global timestamp for the audio frames
    OUTPUTTYPE                  m_eOutputType;

    // Conversion
    AVAudioResampleContext *    m_pResample_context;
    AVAudioFifo *               m_pFifo;

    ID3v1                       m_id3v1;
};

#endif
