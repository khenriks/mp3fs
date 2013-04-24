WARNINGS = -Wall -Wextra
AM_CFLAGS = $(fuse_CFLAGS) $(WARNINGS)
AM_CXXFLAGS = $(fuse_CFLAGS) $(WARNINGS)
bin_PROGRAMS = mp3fs
mp3fs_SOURCES = mp3fs.c fuseops.c transcode.c transcode.h buffer.cc
mp3fs_LDADD	= $(fuse_LIBS)
if HAVE_FLAC
mp3fs_SOURCES +=
mp3fs_LDADD += $(flac_LIBS)
AM_CFLAGS += $(flac_CFLAGS)
endif
if HAVE_MP3
mp3fs_SOURCES += mp3_encoder.cc
mp3fs_LDADD += $(id3tag_LIBS)
AM_CFLAGS += $(id3tag_CFLAGS)
AM_CXXFLAGS += $(id3tag_CFLAGS)
endif
