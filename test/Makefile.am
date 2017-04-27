TESTS =
if HAVE_FLAC
TESTS += test_filenames_flac test_tags_flac test_audio_flac test_filesize_flac
endif
if HAVE_VORBIS
TESTS += test_filenames_vorbis test_tags_vorbis test_audio_vorbis test_filesize_vorbis
endif

EXTRA_DIST = $(TESTS) funcs.sh audio

check_PROGRAMS = fpcompare
fpcompare_SOURCES = fpcompare.c
fpcompare_LDADD = -lchromaprint -lavcodec -lavformat -lavutil
