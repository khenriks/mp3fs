# Definitions for sub-makes
WARNINGS := -Wall -Wextra -Wconversion -Wno-sign-conversion
GIT_VERSION := $(shell test -d .git && git describe)
GIT_VERSION_DEFINE := $(if $(GIT_VERSION),-DGIT_VERSION=\"$(GIT_VERSION)\",)

export AM_CFLAGS = -std=c11 $(INCLUDES) $(WARNINGS) $(GIT_VERSION_DEFINE)
export AM_CXXFLAGS = -std=c++11 $(INCLUDES) $(WARNINGS) $(GIT_VERSION_DEFINE)

SUBDIRS = src test

dist_man_MANS = mp3fs.1

MAINTAINERCLEANFILES = mp3fs.1

EXTRA_DIST = INSTALL.md NEWS.md README.md mp3fs.1.md

DATE_FMT = %B %Y
SOURCE_DATE_EPOCH ?= $(shell date +%s)
if HAVE_GNUDATE
REVDATE = $(shell date -u -d "@$(SOURCE_DATE_EPOCH)" "+$(DATE_FMT)")
else
# Assume non-GNU date must be BSD date. We don't support anything else.
REVDATE = $(shell date -u -r "$(SOURCE_DATE_EPOCH)" "+$(DATE_FMT)")
endif

# This variable is needed because Pandoc on Travis doesn't understand "smart".
mp3fs.1: NOSMART := $(shell pandoc --list-extensions | grep -q smart && echo -smart)
mp3fs.1: mp3fs.1.md
	$(AM_V_GEN)pandoc -s -f markdown$(NOSMART) -t man \
	    -M footer="mp3fs $(VERSION)" \
	    -M date="$(REVDATE)" $< -o $@

# Remove absolutely every generated file
.PHONY: squeaky-clean
squeaky-clean: maintainer-clean
	find . -name Makefile.in -delete
	rm -rf aclocal.m4 configure config

.PHONY: staticcheck mdcheck
staticcheck:
	@statictest/checkall

# The special logic for mp3fs.1.md is because it needs certain features for
# manpage output. The weird sed logic is there to preserve '.\ ' sequences,
# and the check is made automatically passing because the headers aren't
# preserved properly.
mdcheck:
	@set -e ; for f in *.md */*.md ; do \
	    echo "Checking format for $$f." ; \
	    if [ "$$f" = mp3fs.1.md ] ; then \
	    	sed 's/\.\\/!./g' "$$f" | pandoc -d .pandocfmt.yaml -f markdown -t markdown | sed 's/!\./.\\/g' | diff -u $$f - || : ; \
	    else \
	    	pandoc -d .pandocfmt.yaml "$$f" | diff -u "$$f" - ; \
	    fi ; \
	    done
