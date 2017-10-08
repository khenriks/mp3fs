Installation Instructions for mp3fs
===================================

This file is copyright (C) 2013-2014 K. Henriksson. It can be distributed
under the terms of the GFDL 1.3 or later. See README.md for more
information.
FFMPEG support 2017 by Norbert Schllia (nschlia@oblivion-software.de)

Prerequisites
-------------

mp3fs uses FFMEG lib for decoding/encoding. It requires the following 
libraries:

* gcc and g++

* fuse (>= 2.6.0)

* libavutil     (>= 54.3.0)
* libavcodec    (>= 56.1.0)
* libavformat   (>= 56.1.0)
* libavresample (>= 2.1.0)

If building from git, you'll also need:

* autoconf
* automake
* asciidoc
* xmllint
* xmlto

The commands to install just the first prerequisites follow.

On Debian:

    aptitude install gcc g++

    aptitude install libfuse-dev libavcodec-dev libavformat-dev libavresample-dev libavutil-dev
	
To build the docs succesfully you may need

	apt-get install asciidoc
	
Note that for Debian 8 the LIBAV clone of FFMPEG will be installed. From Debian 9 on the original FFMPEG comes with the distribution. Both libraries work.

On Ubuntu use the same command with `apt-get` in place of `aptitude`.

* Tips on other OSes and distributions like Mac or Red-Hat are welcome.

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

Trouble Shooting
----------------

If you get this error:
    
    Running autoreconf --install
    configure.ac:46: error: possibly undefined macro: AC_DEFINE
      If this token and others are legitimate, please use m4_pattern_allow.
      See the Autoconf documentation.
    autoreconf: /usr/bin/autoconf failed with exit status: 1

You are probabibly missing out on pkg-config, either it is not installed or
not in path. "apt-get install pkg-config" shoud help.

    
    
