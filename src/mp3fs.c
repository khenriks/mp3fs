/*
  MP3FS: A read-only FUSE filesystem which transcodes audio formats
  (currently FLAC) to MP3 on the fly when opened and read. This was
  written to enable me to use my FLAC collection with software and/or
  hardware which only understands MP3. e.g. gmediaserver to a netgear
  MP101 mp3 player.
  
  When a file is opened, the decoder and encoder are initialised and
  the file metadata is read. At this time the final filesize can be
  determined as we only support constant bitrate mp3s.
  
  As the file is read, it is transcoded into an internal per-file
  buffer. This buffer continues to grow while the file is being read
  until the whole file is transcoded in memory. The memory is freed
  only when the file is closed. This simplifies the implementation.

  Seeking within a file will cause the file to be transcoded up to the
  seek point (if not already done). This is not usually a problem
  since most programs will read a file from start to finish. 

  A special exception to this is when an application tries to read the
  very last block first. Many applications do this to look for an
  id3v1 tag (stored in the last 128 bytes of the file). When this is
  detected, the filesystem simply return zeros (I dont support id3v1
  tags). This *dramatically* speeds up applications, however it could
  potentially lead to corrupt mp3 files if the zeros are still in
  kernel cache when the application comes back to read the actual
  audio sequentially. In my experimentation this has not happened, I
  always see another read for the final block.

  ID3v2 tags are created when the file is first opened. They are
  located at the start of the file. As such, an application scanning a
  directory to read tags should not cause too much of a performance
  hit as the actual encoder does not need to be invoked (depending on
  how much data the read asks for).

  Copyright (C) David Collett (daveco@users.sourceforge.net)
  
  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#define FUSE_USE_VERSION 22

#include <fuse.h>
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

#include "mp3fs.h"
#include "talloc.h"
#include "list.h"

// a list of currently opened files
static struct FileTranscoder filelist;
static FILE *log;

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
  frac_SpF = (144*MP3_BITRATE*1000) % samplerate;
  
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
  DEBUG(log, "LAME error: ");
#ifdef __DEBUG__
  vfprintf(log, fmt, list);
#endif
  return;
}

// flac callback for errors
void error_cb(const FLAC__FileDecoder *decoder,
	      FLAC__StreamDecoderErrorStatus status,
	      void *client_data) {
  DEBUG(log, "FLAC error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
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
    //    printf("sample_rate: %u\nchannels: %u\nbits/sample: %u\ntotal_samples: %u\n",
    //	   trans->info.sample_rate, trans->info.channels, 
    //	   trans->info.bits_per_sample, trans->info.total_samples);
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
  FLAC__file_decoder_set_metadata_respond(self->decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__file_decoder_init(self->decoder);
  
  // process a single block, the first block is always STREAMINFO (I
  // hope). This will fill in the info structure which is required to
  // initialise the encoder
  FLAC__file_decoder_process_single(self->decoder);
  
  // create encoder
  self->encoder = lame_init();
  lame_set_quality(self->encoder, MP3_QUALITY);
  lame_set_brate(self->encoder, MP3_BITRATE);
  lame_set_bWriteVbrTag(self->encoder, 0);
  lame_set_errorf(self->encoder, &lame_error);
  lame_set_debugf(self->encoder, &lame_error);
  lame_set_msgf(self->encoder, &lame_error);
  lame_set_num_samples(self->encoder, self->info.total_samples);
  lame_set_in_samplerate(self->encoder, self->info.sample_rate);
  lame_set_num_channels(self->encoder, self->info.channels);
  self->framesize = 144*MP3_BITRATE*1000/self->info.sample_rate;
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

static int mp3fs_readlink(const char *path, char *buf, size_t size) {
  int res;
  
  res = readlink(path, buf, size - 1);
  if(res == -1)
    return -errno;
  
  buf[res] = '\0';
  return 0;
}

static int mp3fs_getattr(const char *path, struct stat *stbuf) {
  int res;
  FileTranscoder f;
  
  // pass-through for regular files
  if(lstat(path, stbuf) == 0)
    return 0;
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)path);
  if(lstat(f->orig_name, stbuf) == -1)
    return -errno;
  
  stbuf->st_size = f->totalsize;
  f->Finish(f);
  talloc_free(f);
  
  DEBUG(log, "%s: getattr\n", path);
  return 0;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi) {
  DIR *dp;
  struct dirent *de;
  char name[256], *ptr;

  dp = opendir(path);
  if(dp == NULL)
    return -errno;
  
  DEBUG(log, "%s: readdir\n", path);

  while((de = readdir(dp)) != NULL) {
    struct stat st;
    
    strncpy(name, de->d_name, 256);
    ptr = strstr(name, ".flac");
    if(ptr) {
      strcpy(ptr, ".mp3");
    }
    
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, name, &st, 0))
      break;
  }
  
  closedir(dp);
  return 0;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
  int fd;
  FileTranscoder f;
  
  // If this is a real file, do nothing
  fd = open(path, fi->flags);
  if(fd != -1) {
    close(fd);
    return 0;
  }
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)path);
  
  // add the file to the list
  list_add(&(f->list), &(filelist.list));
  
  DEBUG(log, "%s: open\n", f->orig_name);
  return 0;
}

static int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi) {
  int fd, res;
  struct stat st;
  FileTranscoder f=NULL;
  
  // If this is a real file, allow pass-through
  fd = open(path, O_RDONLY);
  if(fd != -1) {
    res = pread(fd, buf, size, offset);
    if(res == -1)
      res = -errno;
    
    close(fd);
    return res;
  }
  
  list_for_each_entry(f, &(filelist.list), list) {
    if(strcmp(f->name, path) == 0)
      break;
    f=NULL;
  }
  
  if(f==NULL) {
    DEBUG(log, "Tried to read from unopen file: %s\n", path);
    return -errno;
  }
  DEBUG(log, "%s: reading %d from %d\n", path, size, offset);
  return f->Read(f, buf, offset, size);
}

static int mp3fs_statfs(const char *path, struct statfs *stbuf) {
  int res;
  
  res = statfs(path, stbuf);
  if(res == -1)
    return -errno;
  
  return 0;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
  FileTranscoder f=NULL;
  struct stat st;
  
  // pass-through
  if(lstat(path, &st) == 0)
    return 0;
  
  list_for_each_entry(f, &(filelist.list), list) {
    if(strcmp(f->name, path) == 0)
      break;
    f=NULL;
  }
  
  if(f!=NULL) {
    list_del(&(f->list));
    talloc_free(f);
    DEBUG(log, "%s: release\n", path);
  }
  return 0;
}

static struct fuse_operations mp3fs_ops = {
  .getattr = mp3fs_getattr,
  .readlink= mp3fs_readlink,
  .readdir = mp3fs_readdir,
  .open	   = mp3fs_open,
  .read	   = mp3fs_read,
  .statfs  = mp3fs_statfs,
  .release = mp3fs_release,
};

int main(int argc, char *argv[]) {
  
  FileTranscoder f;

  // open the logfile
#ifdef __DEBUG__
  log = fopen("/tmp/mp3fs.log", "w");
#endif
  
  // initialise the open list
  INIT_LIST_HEAD(&(filelist.list));
  
  // start FUSE
  fuse_main(argc, argv, &mp3fs_ops);
  
#ifdef __DEBUG__
  fclose(log);
#endif

  return 0;
}
