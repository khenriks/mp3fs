lib_LIBRARIES = libcodecs.a
libcodecs_a_SOURCES = coders.cc coders.h
INCLUDES = $(fuse_CFLAGS) -I..

if HAVE_FLAC
libcodecs_a_SOURCES += flac_decoder.cc flac_decoder.h
INCLUDES += $(flac_CFLAGS)
endif

if HAVE_VORBIS
libcodecs_a_SOURCES += vorbis_decoder.cc vorbis_decoder.h picture.cc picture.h
INCLUDES += $(vorbis_CFLAGS)
endif

if HAVE_MP3
libcodecs_a_SOURCES += mp3_encoder.cc mp3_encoder.h
INCLUDES += $(id3tag_CFLAGS)
endif
