AM_CFLAGS	= $(fuse_CFLAGS) $(id3tag_CFLAGS) $(flac_CFLAGS) -Wall
bin_PROGRAMS	= mp3fs
mp3fs_SOURCES	= mp3fs.c transcode.c transcode.h
mp3fs_LDADD	= $(fuse_LIBS) $(id3tag_LIBS) $(flac_LIBS)
