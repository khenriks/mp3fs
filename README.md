mp3fs
=====

[![Build Status](https://travis-ci.org/khenriks/mp3fs.svg?branch=master)](https://travis-ci.org/khenriks/mp3fs)

Web site: http://khenriks.github.io/mp3fs/

mp3fs is a read-only FUSE filesystem which transcodes between audio formats
(currently FLAC and Ogg Vorbis to MP3) on the fly when opened and read.

This can let you use a FLAC or Ogg Vorbis collection with software and/or
hardware which only understands the MP3 format, or transcode files through
simple drag-and-drop in a file browser.

For installation instructions see the [install](INSTALL.md) file.

Usage
-----

Mount your filesystem like this:

    mp3fs [-b bitrate] musicdir mountpoint [-o fuse_options]

For example,

    mp3fs -b 128 /mnt/music /mnt/mp3 -o allow_other,ro

In recent versions of FUSE and mp3fs, the same can be achieved with the
following entry in `/etc/fstab`:

    mp3fs#/mnt/music /mnt/mp3 fuse allow_other,ro,bitrate=128 0 0

At this point the files `/mnt/music/**.flac` and `/mnt/music/**.ogg` will
show up as `/mnt/mp3/**.mp3`.

How it Works
------------

When a file is opened, the decoder and encoder are initialised and
the file metadata is read. At this time the final filesize can be
determined as we only support constant bitrate (CBR) MP3 files.

As the file is read, it is transcoded into an internal per-file
buffer. This buffer continues to grow while the file is being read
until the whole file is transcoded in memory. The memory is freed
only when the file is closed. This simplifies the implementation.

Seeking within a file will cause the file to be transcoded up to the
seek point (if not already done). This is not usually a problem
since most programs will read a file from start to finish. Future
enhancements may provide true random seeking.

ID3 version 2.4 and 1.1 tags are created from the vorbis comments in
the FLAC or Ogg Vorbis file. They are located at the start and end of
the file respectively.

A special optimisation is made so that applications which scan for
id3v1 tags do not have to wait for the whole file to be transcoded
before reading the tag. This *dramatically* speeds up such
applications.

Development
-----------

mp3fs uses Git for revision control. You can obtain the full repository
with:

    git clone https://github.com/khenriks/mp3fs.git

mp3fs is written in a mixture of C and C++ and uses the following libraries:

* [FUSE](http://fuse.sourceforge.net/)
* [FLAC](http://flac.sourceforge.net/)
* [libvorbis](http://www.xiph.org/vorbis/)
* [LAME](http://lame.sourceforge.net/)
* [libid3tag](http://www.underbit.com/products/mad/)

Authors
-------

This program is maintained by K. Henriksson, who is the primary author
from 2008 to present.

The original maintainer and author was David Collett from 2006 to 2008.
Much thanks to him for his original work.

License
-------

This program can be distributed under the terms of the GNU GPL version 3
or later. It can be found [online](http://www.gnu.org/licenses/gpl-3.0.html)
or in the COPYING file.

This file and other documentation files can be distributed under the terms of
the GNU Free Documentation License 1.3 or later. It can be found
[online](http://www.gnu.org/licenses/fdl-1.3.html) or in the COPYING.DOC file.
