/*
  MP3FS: A read-only FUSE filesystem which transcodes audio formats
  (currently FLAC) to MP3 on the fly when opened and read. See README
  for more details.
  
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

#define FUSE_USE_VERSION 22

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>

#include "transcode.h"
#include "talloc.h"

static int mp3fs_readlink(const char *path, char *buf, size_t size) {
  int res;
  char name[256];
  
  DEBUG(logfd, "%s: readlink\n", path);

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
  
  DEBUG(logfd, "%s: readdir\n", path);

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));
  
  dp = opendir(name);
  if(dp == NULL)
    return -errno;
  

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
  
  DEBUG(logfd, "%s: getattr\n", path);

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));
  
  // pass-through for regular files
  if(lstat(name, stbuf) == 0)
    return 0;
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
  if(f == NULL)
      return -1;

  if(lstat(f->orig_name, stbuf) == -1) {
    talloc_free(f);
    return -errno;
  }
  
  stbuf->st_size = f->totalsize;
  f->Finish(f);
  talloc_free(f);
  
  return 0;
}

static int mp3fs_open(const char *path, struct fuse_file_info *fi) {
  int fd;
  FileTranscoder f;
  char name[256];

  DEBUG(logfd, "%s: open\n", path);

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  // If this is a real file, do nothing
  fd = open(name, fi->flags);

  // file does exist, but cant be opened, pass the error up
  if(fd == -1 && errno != ENOENT)
      return -errno;

  // file is real and can be opened, return success
  if(fd != -1) {
    close(fd);
    fi->fh = 0;
    return 0;
  }
  
  f = CONSTRUCT(FileTranscoder, FileTranscoder, Con, NULL, (char *)name);
  if(f==NULL)
      return -1;
  
  // store ourselves in the fuse_file_info structure
  fi->fh = (unsigned long) f;
  
  return 0;
}

static int mp3fs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi) {
  int fd, res;
  struct stat st;
  FileTranscoder f=NULL;
  char name[256];

  DEBUG(logfd, "%s: reading %d from %d\n", path, size, offset);

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

  // file does exist, but cant be opened, pass the error up
  if(fd == -1 && errno != ENOENT)
      return -errno;
  
  // retrieve transcoder
  f = (FileTranscoder) fi->fh;

  if(f == NULL) {
    DEBUG(logfd, "Tried to read from unopen file: %s\n", name);
    return -errno;
  }
  return f->Read(f, buf, offset, size);
}

static int mp3fs_statfs(const char *path, struct statfs *stbuf) {
  int res;
  char name[256];

  DEBUG(logfd, "%s: statfs\n", path);

  strncpy(name, basepath, sizeof(name));
  strncat(name, path, sizeof(name) - strlen(name));

  res = statfs(name, stbuf);
  if(res == -1)
    return -errno;
  
  return 0;
}

static int mp3fs_release(const char *path, struct fuse_file_info *fi) {
  FileTranscoder f;

  DEBUG(logfd, "%s: release\n", path);
  
  f = (FileTranscoder) fi->fh;
  if(f) {
    f->Finish(f);
    talloc_free(f);
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

void usage(char *name) {
  printf("\nUSAGE: %s flacdir bitrate mountpoint [-o fuseopts]\n", name);
  printf("  acceptable bitrates are 96, 112, 128, 160, 192, 224, 256, 320\n");
  printf("  for a list of fuse options use -h after mountpoint\n\n");
}

int main(int argc, char *argv[]) {
  
  if(argc<3) {
    usage(argv[0]);
    return 0;
  }
  
  basepath = argv[1];
  bitrate = atoi(argv[2]);
  
  // open the logfile
#ifdef __DEBUG__
  logfd = fopen("/tmp/mp3fs.log", "w");
#endif
  
  // start FUSE
  fuse_main(argc-2, argv+2, &mp3fs_ops);
  
#ifdef __DEBUG__
  fclose(logfd);
#endif

  return 0;
}
