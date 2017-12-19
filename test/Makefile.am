TESTS = test_filenames_mp4 test_filenames_mp3 test_tags_mp4 test_tags_mp3 test_audio_mp4 test_audio_mp3 test_filesize_mp4 test_filesize_mp3 
# NOT IN RELEASE 1.0!  test_picture_mp4 test_picture_mp3

EXTRA_DIST = $(TESTS) funcs.sh srcdir

CLEANFILES = $(patsubst %,%.builtin.log,$(TESTS))

check_PROGRAMS = fpcompare
fpcompare_SOURCES = fpcompare.c
fpcompare_LDADD = -lchromaprint -lavcodec -lavformat -lavutil -lswresample
