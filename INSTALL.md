Installation Instructions for mp3fs
===================================

This file is copyright (C) 2013-2014 K. Henriksson. It can be distributed
under the terms of the GFDL 1.3 or later. See README.md for more
information.

Prerequisites
-------------

mp3fs requires the following libraries:

* fuse (>= 2.6.0)
* flac++ (>= 1.1.4)
* libvorbis (>= 1.3.0)
* lame
* libid3tag

If building from git, you'll also need:

* autoconf
* automake
* asciidoc
* xmllint
* xmlto

The commands to install the prerequisites follow.

### Debian

    aptitude install libfuse-dev libflac++-dev libvorbis-dev libmp3lame-dev libid3tag0-dev

If building from git:

    aptitude install autoconf automake asciidoc

### Ubuntu

    apt install libfuse-dev libflac++-dev libvorbis-dev libmp3lame-dev libid3tag0-dev

If building from git:

    apt install autoconf automake asciidoc

### macOS with Homebrew

    brew install osxfuse flac libvorbis lame libid3tag

If building from git:

    brew install autoconf automake asciidoc xmlto

### RedHat-type systems, with the right repositories

    yum install fuse-devel flac-devel libvorbis-devel lame-devel libid3tag-devel

Installation
------------

**mp3fs** uses the GNU build system.

If you are installing from git, you'll need to first run:

    ./autogen.sh

If you are downloading a release, this has already been done for you.

To build and install, run:

    ./configure
    make
    make install
