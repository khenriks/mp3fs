Installation Instructions for mp3fs
===================================

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
* pandoc

The commands to install the prerequisites follow.

### Debian

    aptitude install libfuse-dev libflac++-dev libvorbis-dev libmp3lame-dev libid3tag0-dev

If building from git:

    aptitude install autoconf automake pandoc

### Ubuntu

    apt install libfuse-dev libflac++-dev libvorbis-dev libmp3lame-dev libid3tag0-dev

If building from git:

    apt install autoconf automake pandoc

### macOS with Homebrew

    brew install osxfuse flac libvorbis lame libid3tag

If building from git:

    brew install autoconf automake pandoc

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

License
-------

This file is copyright (C) 2013-2014 K. Henriksson.

This documentation may be distributed under the GNU Free Documentation License
(GFDL) 1.3 or later with no invariant sections, or alternatively under the GNU
General Public License (GPL) version 3 or later.
