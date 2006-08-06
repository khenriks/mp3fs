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

#include "transcode.h"
#include "talloc.h"
#include "list.h"


static int mp3fs_readlink(const char *path, char *buf, size_t size) {
  int res;
  char name[256];
  
  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  res = readlink(name, buf, size - 1);
  if(res == -1)
    return -errno;
  
  buf[res] = '\0';
  return 0;
}

static int mp3fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi) {
  DIR *dp;
  struct dirent *de;
  char name[256], *ptr;
  
  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));
  
  dp = opendir(name);
  if(dp == NULL)
    return -errno;
  
  DEBUG(logfd, "%s: readdir\n", name);

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

static int mp3fs_getattr(const char *path, struct stat *stbuf) {
  int res;
  FileTranscoder f;
  char name[256];
  
  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));
  
  // pass-through for regular files
  if(lstat(name, stbuf) == 0)
    return 0;
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
  if(lstat(f->orig_name, stbuf) == -1)
    return -errno;
  
  stbuf->st_size = f->totalsize;
  f->Finish(f);
  talloc_free(f);
  
  DEBUG(logfd, "%s: getattr\n", name);
  return 0;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
  int fd;
  FileTranscoder f;
  char name[256];

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  // If this is a real file, do nothing
  fd = open(name, fi->flags);
  if(fd != -1) {
    close(fd);
    return 0;
  }
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
  
  // add the file to the list
  list_add(&(f->list), &(filelist.list));
  
  DEBUG(logfd, "%s: open\n", f->name);
  return 0;
}

static int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi) {
  int fd, res;
  struct stat st;
  FileTranscoder f=NULL;
  char name[256];

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));
  
  // If this is a real file, allow pass-through
  fd = open(name, O_RDONLY);
  if(fd != -1) {
    res = pread(fd, buf, size, offset);
    if(res == -1)
      res = -errno;
    
    close(fd);
    return res;
  }
  
  list_for_each_entry(f, &(filelist.list), list) {
    if(strcmp(f->name, name) == 0)
      break;
    f=NULL;
  }
  
  if(f==NULL) {
    DEBUG(logfd, "Tried to read from unopen file: %s\n", name);
    return -errno;
  }
  DEBUG(logfd, "%s: reading %d from %d\n", name, size, offset);
  return f->Read(f, buf, offset, size);
}

static int mp3fs_statfs(const char *path, struct statfs *stbuf) {
  int res;
  char name[256];

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  res = statfs(name, stbuf);
  if(res == -1)
    return -errno;
  
  return 0;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
  FileTranscoder f=NULL;
  struct stat st;
  char name[256];

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  // pass-through
  if(lstat(name, &st) == 0)
    return 0;
  
  list_for_each_entry(f, &(filelist.list), list) {
    if(strcmp(f->name, name) == 0)
      break;
    f=NULL;
  }
  
  if(f!=NULL) {
    list_del(&(f->list));
    talloc_free(f);
    DEBUG(logfd, "%s: release\n", name);
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
  logfd = fopen("/tmp/mp3fs.log", "w");
#endif
  
  // initialise the open list
  INIT_LIST_HEAD(&(filelist.list));
  
  // start FUSE
  basepath = argv[1];
  fuse_main(argc-1, argv+1, &mp3fs_ops);
  
#ifdef __DEBUG__
  fclose(logfd);
#endif

  return 0;
}
