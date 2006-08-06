bin_PROGRAMS	= mp3fs
mp3fs_SOURCES	= mp3fs.c transcode.c transcode.h class.c class.h stringio.c stringio.h talloc.c talloc.h list.h
mp3fs_CFLAGS	= $(fuse_CFLAGS)
mp3fs_LDFLAGS	= -lFLAC -lmp3lame $(fuse_LIBS)
