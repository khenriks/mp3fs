Installation Instructions for mp3fs
===================================

Copyright (C) 2017 Norbert Schlia (nschlia@oblivion-software.de)
This file was originally copyright (C) 2013-2014 K. Henriksson. 

It can be distributed under the terms of the GFDL 1.3 or later. 
See README.md for more information.

Prerequisites
-------------

mp3fs uses FFMEG lib for decoding/encoding. It requires the following 
libraries:

* gcc and g++ compilers

* fuse (>= 2.6.0)

* libavutil      (>= 54.3.0)
* libavcodec     (>= 56.1.0)
* libavformat    (>= 56.1.0)
* libavfilter     6. 82.100
* libavresample  (>= 2.1.0)
* libswscale     (>= 3.0.0)

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
	
Note that for Debian 8 the LIBAV 11 clone of FFMPEG will be installed. 
From Debian 9 on the original FFMPEG comes with the distribution. 
Both libraries work, but please read the FFMPEG/LIBAV version chapter.

On Ubuntu use the same command with `apt-get` in place of `aptitude`.

* Tips on other OSes and distributions like Mac or Red-Hat are welcome.

FFMPEG/LIBAV Version
--------------------

mp3fs will compile fine with Libav 11 (coming with Debian 8 "Squeeze")
and FFMPEG, but if you intend to use the mp4 target format it may be
necessary to use a newer version.

For direct to stream transcoding several new features in mp4 need to
be used (ISMV, faststart, separate_moof/empty_moov to name them) 
which are not implemented in older versions (or if available, not 
working properly). 

Streaming while transcoding does not work with Libav 11 (the version
that comes with Debian 8). You need to replace it with a recent
FFMPEG version. Maybe Libav 12 will work, but this has not been
tested.

With Libav, the first time a file is accessed the playback will fail.
After it has been decoded fully to cache playback will work. Playing
the file via http may fail and it may take quite long until the
file starts playing.

This is a LIBAV problem. Generally I recommend using FFMPEG instead.

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
    
To build and run the check suite, do:    

    make checks
    
This will test audio conversion, tagging, size prediction and image embedding.

NOTE: Size prediction is not working properly at the moment, image embedding
is not yet implemented. Both tests will currently fail.

Trouble Shooting
----------------

If you run into this error:
    
    Running autoreconf --install
    configure.ac:46: error: possibly undefined macro: AC_DEFINE
      If this token and others are legitimate, please use m4_pattern_allow.
      See the Autoconf documentation.
    autoreconf: /usr/bin/autoconf failed with exit status: 1

You are probabibly missing out on pkg-config, either it is not installed or
not in path. "apt-get install pkg-config" (on Debian or equivalent on other
Linux distributions) should help.
