INCLUDES = $(fuse_CFLAGS)

bin_PROGRAMS = mp3fs
mp3fs_SOURCES = mp3fs.c fuseops.c transcode.cc transcode.h buffer.cc buffer.h coders.cc coders.h stats_cache.cc stats_cache.h logging.cc logging.h
mp3fs_LDADD	= $(fuse_LIBS)

if HAVE_FLAC
mp3fs_SOURCES += flac_decoder.cc flac_decoder.h
mp3fs_LDADD += $(flac_LIBS)
INCLUDES += $(flac_CFLAGS)
endif

if HAVE_VORBIS
mp3fs_SOURCES += vorbis_decoder.cc vorbis_decoder.h
mp3fs_LDADD += $(vorbis_LIBS)
INCLUDES += $(vorbis_CFLAGS)
endif

if HAVE_VORBIS_PICTURE
mp3fs_SOURCES += picture.cc picture.h
mp3fs_LDADD += -lb64
endif

if HAVE_MP3
mp3fs_SOURCES += mp3_encoder.cc mp3_encoder.h
mp3fs_LDADD += $(id3tag_LIBS)
INCLUDES += $(id3tag_CFLAGS)
endif
