AM_CPPFLAGS = $(fuse_CFLAGS)

bin_PROGRAMS = mp3fs
mp3fs_SOURCES = mp3fs.c fuseops.c transcode.cc transcode.h buffer.cc buffer.h coders.cc coders.h stats_cache.cc stats_cache.h logging.cc logging.h
mp3fs_LDADD	= $(fuse_LIBS)

if HAVE_FFMPEG
mp3fs_SOURCES += ffmpeg_transcoder.cc ffmpeg_transcoder.h ffmpeg_utils.cc ffmpeg_utils.h
mp3fs_LDADD += $(libavcodec_LIBS) $(libavutil_LIBS) $(libavformat_LIBS) $(libavresample_LIBS) $(libswresample_LIBS) $(libswscale_LIBS)
AM_CPPFLAGS += $(libavcodec_CFLAGS) $(libavutil_CFLAGS) $(libavformat_CFLAGS) $(libavresample_CFLAGS) $(libswresample_CFLAGS) $(libswscale_CFLAGS)
endif
