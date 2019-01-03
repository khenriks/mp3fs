INCLUDES = $(fuse_CFLAGS)

bin_PROGRAMS = mp3fs
mp3fs_SOURCES = mp3fs.cc fuseops.cc transcode.cc transcode.h buffer.cc buffer.h stats_cache.cc stats_cache.h logging.cc logging.h reader.h path.cc path.h
mp3fs_LDADD	= $(fuse_LIBS)

SUBDIRS = codecs lib
mp3fs_LDADD += codecs/libcodecs.a lib/libbase64.a $(flac_LIBS) $(vorbis_LIBS) $(id3tag_LIBS)
