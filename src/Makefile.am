WARNINGS = -Wall -Wextra -Wconversion -Wno-sign-conversion
AM_CFLAGS = -std=gnu99 $(fuse_CFLAGS) $(WARNINGS)
AM_CXXFLAGS = -std=c++98 $(fuse_CFLAGS) $(WARNINGS)
bin_PROGRAMS = mp3fs
mp3fs_SOURCES = mp3fs.c fuseops.c transcode.cc transcode.h buffer.cc buffer.h coders.cc coders.h
mp3fs_LDADD	= $(fuse_LIBS)
if HAVE_FLAC
mp3fs_SOURCES += flac_decoder.cc flac_decoder.h
mp3fs_LDADD += $(flac_LIBS)
AM_CFLAGS += $(flac_CFLAGS)
AM_CXXFLAGS += $(flac_CFLAGS)
endif
if HAVE_VORBIS
mp3fs_SOURCES += vorbis_decoder.cc vorbis_decoder.h
mp3fs_LDADD += $(vorbisfile_LIBS)
AM_CFLAGS += $(vorbisfile_CFLAGS)
AM_CXXFLAGS += $(vorbisfile_CFLAGS)
endif
if HAVE_MP3
mp3fs_SOURCES += mp3_encoder.cc mp3_encoder.h
mp3fs_LDADD += $(id3tag_LIBS)
AM_CFLAGS += $(id3tag_CFLAGS)
AM_CXXFLAGS += $(id3tag_CFLAGS)
endif
