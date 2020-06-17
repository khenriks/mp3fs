TESTS = test_audio \
	test_concurrent \
	test_corrupt \
	test_filenames \
	test_filesize \
	test_passthrough \
	test_picture \
	test_readlink \
	test_tags

EXTRA_DIST = $(TESTS) funcs.sh srcdir

CLEANFILES = $(patsubst %,%.builtin.log,$(TESTS))

check_PROGRAMS = fpcompare concurrent_read
fpcompare_SOURCES = fpcompare.c
fpcompare_LDADD = -lchromaprint -lsox
concurrent_read_SOURCES = concurrent_read.cc
concurrent_read_LDFLAGS = -pthread
