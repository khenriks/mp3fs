AM_CPPFLAGS = $(fuse_CFLAGS)

bin_PROGRAMS = ffmpegfs
ffmpegfs_SOURCES = ffmpegfs.c fuseops.c transcode.cc transcode.h cache.cc cache.h buffer.cc buffer.h coders.cc coders.h logging.cc logging.h cache_entry.cc cache_entry.h id3v1tag.h
ffmpegfs_LDADD = $(fuse_LIBS)

#if HAVE_FFMPEG
ffmpegfs_SOURCES += ffmpeg_transcoder.cc ffmpeg_transcoder.h ffmpeg_utils.cc ffmpeg_utils.h
ffmpegfs_LDADD += $(libavcodec_LIBS) $(libavutil_LIBS) $(libavformat_LIBS) $(libavresample_LIBS) $(libswscale_LIBS)
AM_CPPFLAGS += $(libavcodec_CFLAGS) $(libavutil_CFLAGS) $(libavformat_CFLAGS) $(libavresample_CFLAGS) $(libswscale_CFLAGS)
#endif

ffmpegfs_LDADD += $(sqlite3_LIBS)
