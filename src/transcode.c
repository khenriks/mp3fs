/*
  FileTranscoder interface for MP3FS
  
  Copyright (C) David Collett (daveco@users.sourceforge.net)
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>

#include <FLAC/metadata.h>
#include <FLAC/file_decoder.h>
#include <lame/lame.h>

#include "transcode.h"
#include "talloc.h"
#include "list.h"

/*******************************************************************
 CALLBACKS and HELPERS for LAME and FLAC
*******************************************************************/

/* return a vorbis comment tag */
const char *get_tag(const FLAC__StreamMetadata *metadata, char *name) {
  int idx;
  FLAC__StreamMetadata_VorbisComment *comment;
  comment = (FLAC__StreamMetadata_VorbisComment *)&metadata->data;
  idx = FLAC__metadata_object_vorbiscomment_find_entry_from(metadata, 0, name);
  if(idx<0) return NULL;
  
  return (const char *) (comment->comments[idx].entry + strlen(name) + 1) ;
}

/* calculate the size of the mp3 data */
int calcsize(int framesize, int numframes, int samplerate) {
  int slot_lag;
  int frac_SpF;
  int size=0, i;
  
  // initialize
  slot_lag = 0;
  frac_SpF = (144*bitrate*1000) % samplerate;
  
  // calculate
  for(i=0; i<=numframes; i++) {
    size += framesize;
    if ((slot_lag -= frac_SpF) < 0) {
      slot_lag += samplerate;
      size++;
    }
  }
  return size;
}

// lame callback for error/debug/msg
void lame_error(const char *fmt, va_list list) {
  DEBUG(logfd, "LAME error: ");
#ifdef __DEBUG__
  vfprintf(logfd, fmt, list);
#endif
  return;
}

// flac callback for errors
void error_cb(const FLAC__FileDecoder *decoder,
	      FLAC__StreamDecoderErrorStatus status,
	      void *client_data) {
  DEBUG(logfd, "FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
  return;
}

// callbacks for the decoder
FLAC__StreamDecoderWriteStatus write_cb(const FLAC__FileDecoder *decoder, 
					const FLAC__Frame *frame,
					const FLAC__int32 *const buffer[],
					void *client_data) {
  int len, i, count;
  FileTranscoder trans = (FileTranscoder)client_data;
  
  if(frame->header.blocksize < 1152) {
    //printf("ERROR: got less than a frame: %d\n", frame->header.blocksize);
  }

  // convert down to shorts
  for(i=0; i<frame->header.blocksize; i++) {
    trans->lbuf[i] = (short int)buffer[0][i];
    trans->rbuf[i] = (short int)buffer[1][i];    
  }

  len = lame_encode_buffer(trans->encoder,
			   trans->lbuf, trans->rbuf,
			   frame->header.blocksize,
			   trans->mp3buf, BUFSIZE);
  
  CALL(trans->buffer, write, (char *)trans->mp3buf, len);
  
  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;    
}

void meta_cb(const FLAC__FileDecoder *decoder,
	     const FLAC__StreamMetadata *metadata,
	     void *client_data) {
  
  FileTranscoder trans = (FileTranscoder)client_data;
  
  switch(metadata->type) {
  case FLAC__METADATA_TYPE_STREAMINFO:
    memcpy(&trans->info, &metadata->data, sizeof(FLAC__StreamMetadata_StreamInfo));
/*     DEBUG(logfd, "%s: sample_rate: %u\nchannels: %u\nbits/sample: %u\ntotal_samples: %u\n", */
/* 	  trans->name, */
/* 	  trans->info.sample_rate, trans->info.channels,  */
/* 	  trans->info.bits_per_sample, trans->info.total_samples); */
    break;
  case FLAC__METADATA_TYPE_VORBIS_COMMENT:
    id3tag_init(trans->encoder);
    id3tag_add_v2(trans->encoder);
    id3tag_v2_only(trans->encoder);
    id3tag_set_title(trans->encoder, talloc_strdup(trans, get_tag(metadata, "title")));
    id3tag_set_album(trans->encoder, talloc_strdup(trans, get_tag(metadata, "album")));
    id3tag_set_artist(trans->encoder, talloc_strdup(trans, get_tag(metadata, "artist")));
    id3tag_set_track(trans->encoder, talloc_strdup(trans, get_tag(metadata, "tracknumber")));
    break;
  default:
    break;
  }
}

/*******************************************************************
 FileTranscoder Class implementation
*******************************************************************/

FileTranscoder FileTranscoder_Con(FileTranscoder self, char *filename) {
  self->buffer = CONSTRUCT(StringIO, StringIO, Con, self);
  self->name = talloc_strdup(self, filename);

  // set the original (flac) filename
  self->orig_name = talloc_size(self, strlen(self->name) + 2);
  strncpy(self->orig_name, self->name, strlen(self->name));
  
  // translate name back to original
  {
    char *ptr = strstr(self->orig_name, ".mp3");
    if(ptr)
      strcpy(ptr, ".flac");
  }
  
  // create and initialise decoder
  self->decoder = FLAC__file_decoder_new();
  FLAC__file_decoder_set_filename(self->decoder, self->orig_name);
  FLAC__file_decoder_set_client_data(self->decoder, (void *)self);
  FLAC__file_decoder_set_write_callback(self->decoder, &write_cb);
  FLAC__file_decoder_set_metadata_callback(self->decoder, &meta_cb);
  FLAC__file_decoder_set_error_callback(self->decoder, &error_cb);
  FLAC__file_decoder_set_metadata_respond(self->decoder, 
					  FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__file_decoder_init(self->decoder);
  
  // process a single block, the first block is always
  // STREAMINFO. This will fill in the info structure which is
  // required to initialise the encoder
  FLAC__file_decoder_process_single(self->decoder);
  
  // create encoder
  self->encoder = lame_init();
  lame_set_quality(self->encoder, MP3_QUALITY);
  lame_set_brate(self->encoder, bitrate);
  lame_set_bWriteVbrTag(self->encoder, 0);
  lame_set_errorf(self->encoder, &lame_error);
  lame_set_debugf(self->encoder, &lame_error);
  lame_set_msgf(self->encoder, &lame_error);
  lame_set_num_samples(self->encoder, self->info.total_samples);
  lame_set_in_samplerate(self->encoder, self->info.sample_rate);
  lame_set_num_channels(self->encoder, self->info.channels);
  self->framesize = 144*bitrate*1000/self->info.sample_rate;
  self->numframes = (int)((self->info.total_samples + 575.5)/1152.0);//+1;
  
  // Now process the rest of the metadata. This will fill in the
  // id3tags in the lame encoder.
  FLAC__file_decoder_process_until_end_of_metadata(self->decoder);
  
  // now we can initialise the encoder
  lame_init_params(self->encoder);
  
  // Once the encoder is initialised, we can query it about how much
  // data it has in its buffer, this is exactly the size of our ID3v2
  // tag as we have not yet decoded any audio. We can use this size to
  // determine the final filesize with 100% accuracy (I think)
  self->totalsize = calcsize(self->framesize, self->numframes, self->info.sample_rate)
    + lame_get_size_mp3buffer(self->encoder);
  
  return self;
}

int FileTranscoder_Finish(FileTranscoder self) {
  int len;
  // flac cleanup
  FLAC__file_decoder_finish(self->decoder);  
  FLAC__file_decoder_delete(self->decoder);
  
  // lame cleanup
  len = lame_encode_flush(self->encoder, self->mp3buf, BUFSIZE);
  if(len>0)
    CALL(self->buffer, write, (char *)self->mp3buf, len);
  
  lame_close(self->encoder);
  
  return len;
}

int FileTranscoder_Read(FileTranscoder self, char *buff, int offset, int len) {
  // is this the last block?
  if(offset+len > self->totalsize) {
    len = self->totalsize - offset;
  }
  
  // this is an optimisation to speed up the case where applications
  // read the last block first looking for an id3v1 tag (last 128
  // bytes). If we detect this case, just give back zeros. This hack
  // could very well cause file corruption if the zeros remain in
  // kernel cache when the app finally comes to read the audio
  // data. In practice this doesn't seem to happen.
  if(offset > self->buffer->size &&
     offset + len > (self->totalsize - 128)) {
    memset(buff, 0, len);
    return len;
  }
  
  // transcode up to what we need
  while(self->buffer->size < offset + len) {
    if(FLAC__file_decoder_get_state(self->decoder)==0) {
      FLAC__file_decoder_process_single(self->decoder);
    } else {
      self->Finish(self);
      break;
    }
  }
  
  memcpy(buff, self->buffer->data+offset, len);
  return len;
}

VIRTUAL(FileTranscoder, Object)
  VATTR(readptr) = 0;
  VMETHOD(Con) = FileTranscoder_Con;
  VMETHOD(Read) = FileTranscoder_Read;
  VMETHOD(Finish) = FileTranscoder_Finish;
END_VIRTUAL
