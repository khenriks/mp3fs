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

#include "list.h"
#include "class.h"
#include "stringio.h"

#define MP3_BITRATE 320
#define MP3_QUALITY 5
#define FLAC_BLOCKSIZE 4608
#define BUFSIZE 2 * FLAC_BLOCKSIZE

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
