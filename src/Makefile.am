bin_PROGRAMS	= mp3fs
mp3fs_SOURCES	= mp3fs.c transcode.c transcode.h class.c class.h stringio.c stringio.h talloc.c talloc.h
mp3fs_CFLAGS	= $(fuse_CFLAGS) $(id3tag_CFLAGS)
mp3fs_LDFLAGS	= -lFLAC -logg -lmp3lame $(fuse_LIBS) $(id3tag_LIBS)
