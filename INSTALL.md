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

Please read the "Supported Linux Distributions" chapter in README.md 
for details.

On Debian:

    aptitude install gcc g++

    aptitude install libfuse-dev libavcodec-dev libavformat-dev libavresample-dev libavutil-dev libswscale-dev

On Ubuntu use the same command with `apt-get` in place of `aptitude`.

On Suse (please read notes before continuing):

    zypper install gcc gcc-c++

    zypper install fuse-devel libavcodec-devel libavformat-devel libavresample-devel libavutil-devel libswscale-devel

Suse includes non-proprietary codecs with FFmpeg, namely mp3, AAC and H264 
are *not* avaibable which renders this library usesless. But FFmpeg can be
built from source, see https://trac.ffmpeg.org/wiki/CompilationGuide and check
"FFmpeg compile notes" below.

On Red Hat:

    yum install gcc g++

    yum install fuse-devel

Red Hat does not provide FFmpeg from its repositories. It must be built 
from source code, see this guide: https://trac.ffmpeg.org/wiki/CompilationGuide/Centos

If you want to build the documentation you will find "asciidoc" missing from
the Red Hat repositories. To get it use a beta repository:

    yum --enablerepo=rhel-7-server-optional-beta-rpms install asciidoc

FFmpeg compile notes:

FFmpeg must be built with at least libx264, libfdk_aac and libmp3lame support. 
Other libraries, e.g. ogg, Windows Media or FLAC must be added when these
formats should be used as source.

libavresample will not be built by default. Add "--enable-avresample" to the configure
command line to include it.

If you run into this "ERROR: libmp3lame >= 3.98.3 not found" although you have built 
and installed libmp3lame you may find a solution here: 
https://stackoverflow.com/questions/35937403/error-libmp3lame-3-98-3-not-found

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

You are probably missing out on pkg-config, either it is not installed or
not in path. "apt-get install pkg-config" (on Debian or equivalent on other
Linux distributions) should help.
