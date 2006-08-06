/*
  Copyright (C) David Collett (daveco@users.sourceforge.net)
  
  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include <FLAC/metadata.h>
#include <FLAC/file_decoder.h>
#include <lame/lame.h>

#include "list.h"
#include "class.h"
#include "stringio.h"

#define MP3_BITRATE 320
#define MP3_QUALITY 5
#define FLAC_BLOCKSIZE 4608
#define BUFSIZE 2 * FLAC_BLOCKSIZE

// a list of currently opened files
struct FileTranscoder filelist;
FILE *logfd;
const char *basepath;

/** This is used for debugging. */
#ifndef __DEBUG__
#define DEBUG(f, x, ...)
#else
#define DEBUG(f, x, ...) do {				 \
    fprintf(f, "%s:%u ",__FUNCTION__,__LINE__);		 \
    fprintf(f, x, ## __VA_ARGS__);			 \
    fflush(f);						 \
  } while(0)
#endif

CLASS(FileTranscoder, Object)
  StringIO buffer;
  int readptr;
  int framesize;
  int numframes;
  int totalsize;
  char *name;
  char *orig_name;

  lame_global_flags *encoder;
  FLAC__FileDecoder *decoder;
  FLAC__StreamMetadata_StreamInfo info;
  short int lbuf[FLAC_BLOCKSIZE];
  short int rbuf[FLAC_BLOCKSIZE];
  unsigned char mp3buf[BUFSIZE];

  FileTranscoder METHOD(FileTranscoder, Con, char *filename);
  int METHOD(FileTranscoder, Read, char *buff, int offset, int len);
  int METHOD(FileTranscoder, Finish);

  // allow this object to be part of a list
  struct list_head list;
END_CLASS
