TESTS = test_filenames test_tags test_audio test_filesize test_picture test_corrupt test_concurrent test_passthrough

EXTRA_DIST = $(TESTS) funcs.sh srcdir

CLEANFILES = $(patsubst %,%.builtin.log,$(TESTS))

check_PROGRAMS = fpcompare concurrent_read
fpcompare_SOURCES = fpcompare.c
fpcompare_LDADD = -lchromaprint -lsox
concurrent_read_SOURCES = concurrent_read.cc
concurrent_read_LDFLAGS = -pthread
