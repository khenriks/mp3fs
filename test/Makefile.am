TESTS = test_filenames test_tags test_audio test_filesize test_picture

EXTRA_DIST = $(TESTS) funcs.sh flac

CLEANFILES = $(patsubst %,%.builtin.log,$(TESTS))

check_PROGRAMS = fpcompare
fpcompare_SOURCES = fpcompare.c
fpcompare_LDADD = -lchromaprint -lavcodec -lavformat -lavutil -lswresample
