Installation Instructions for mp3fs
===================================

This file is copyright (C) 2013-2014 K. Henriksson. It can be distributed
under the terms of the GFDL 1.3 or later. See README.md for more
information.
FFMPEG support 2017 by Norbert Schllia (nschlia@oblivion-software.de)

Prerequisites
-------------

mp3fs can be installed with FFMEG for decoding/encoding or with flac++/libvorbis/lame/id3tag.
It requires the following libraries:

In any case:

* fuse (>= 2.6.0)

If using the FFMPEG support:

* libavutil     (>= 54.3.0)
* libavcodec    (>= 56.1.0)
* libavformat   (>= 56.1.0)
* libavresample (>= 2.1.0)

These are only required if not using FFMPEG (configure with --with-ffmpeg=no or do not install the FFMPEG libraries):

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

The commands to install just the first four prerequisites follow.

On Debian (FFMPEG):

	aptitude install libavcodec56

or

	aptitude install libavcodec-extra-56
 
and

	aptitude install libavformat56 libavresample2 libavutil54 

Note that for Debian 8 the LIBAV clone of FFMPEG will be installed. From Debian 9 on the original FFMPEG comes with the distribution. Both libraries work.

On Debian (no FFMPEG):

    aptitude install libfuse-dev libflac++-dev libvorbis-dev libmp3lame-dev libid3tag0-dev

On Ubuntu use the same command with `apt-get` in place of `aptitude`.

On OS X with Homebrew (FFMPEG may work, but untested):

    brew install osxfuse flac libvorbis lame libid3tag

On a RedHat-type systems, with the right repositories (FFMPEG may work, but untested):

    yum install fuse-devel flac-devel libvorbis-devel lame-devel libid3tag-devel

Installation
------------

mp3fs uses the GNU build system. If you are installing from git, you'll
need to first run:

    ./autogen.sh

If you are downloading a release, this has already been done for you. To
build and install, run:

    ./configure
    make
    make install
