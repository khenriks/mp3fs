MP3FS
=====

:Maintainer: Kristofer Henriksson (kthenry@users.sourceforge.net)
:Author: David Collett (daveco@users.sourceforge.net)
:Web site: http://mp3fs.sourceforge.net/

.. contents::

Introduction
------------

MP3FS is a read-only FUSE filesystem which transcodes audio formats
(currently FLAC) to MP3 on the fly when opened and read. This was
written to enable me to use my FLAC collection with software and/or
hardware which only understands the MP3 format e.g. gmediaserver to a
Netgear MP101 MP3 player.

It is also a novel alternative to traditional MP3 encoders. Just use your
favorite file browser to select the files you want encoded and copy them
somewhere!

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
the FLAC file. They are located at the start and end of the file
respectively.

A special optimisation is made so that applicatins which scan for
id3v1 tags do not have to wait for the whole file to be transcoded
before reading the tag. This *dramatically* speeds up such
applications.

For build instructions see the bundled INSTALL file.

Usage
-----

**NOTE:** The command line format changed starting with version 0.10,
adding a comma.

Mount your filesystem like this::

  mp3fs musicdir,bitrate mountpoint [-o fuse_options]

e.g.::

  mp3fs /mnt/music,128 /mnt/mp3 -o allow_other,ro

In recent versions of FUSE and MP3FS, the same can be achieved with the
following entry in ``/etc/fstab``::

  mp3fs#/mnt/music,128 /mnt/mp3 fuse allow_other,ro 0 0

Note that this requires /sbin/mount.fuse from the fuse-utils package.

At this point the filesystem is ready to be used. Here are the original
files::

  dave@bender:~/mp3fs$ ls -l /mnt/music/Smashing\ Pumpkins/Pisces\ Iscariot/
  total 345732
  -rw-r--r-- 1 mythtv mythtv 10267876 2005-06-19 18:36 01 - Soothe.flac
  -rw-r--r-- 1 mythtv mythtv 23512276 2005-06-19 18:36 02 - Frail And Bedazzled.flac
  -rw-r--r-- 1 mythtv mythtv 23332187 2005-06-19 18:36 03 - Plum.flac
  -rw-r--r-- 1 mythtv mythtv 26402936 2005-06-19 18:36 04 - Whir.flac
  -rw-r--r-- 1 mythtv mythtv 21591252 2005-06-19 18:36 05 - Blew Away.flac
  -rw-r--r-- 1 mythtv mythtv 16719855 2005-06-19 18:36 06 - Pissant.flac
  -rw-r--r-- 1 mythtv mythtv 33454889 2005-06-19 18:36 07 - Hello Kitty Kat.flac
  -rw-r--r-- 1 mythtv mythtv 32073747 2005-06-19 18:36 08 - Obscured.flac
  -rw-r--r-- 1 mythtv mythtv 17614217 2005-06-19 18:36 09 - Landslide.flac
  -rw-r--r-- 1 mythtv mythtv 65406696 2005-06-19 18:36 10 - Starla.flac
  -rw-r--r-- 1 mythtv mythtv 18651734 2005-06-19 18:36 11 - Blue.flac
  -rw-r--r-- 1 mythtv mythtv 25055200 2005-06-19 18:36 12 - Girl Named Sandoz.flac
  -rw-r--r-- 1 mythtv mythtv 28060023 2005-06-19 18:36 13 - La Dolly Vita.flac
  -rw-r--r-- 1 mythtv mythtv 11432008 2005-06-19 18:36 14 - Spaced.flac

And now you can use the (virtual) MP3 files from the MP3FS mountpoint::

  dave@bender:~/mp3fs$ ls -l /mnt/mp3/Smashing\ Pumpkins/Pisces\ Iscariot/
  total 345732
  -rw-r--r-- 1 mythtv mythtv  2446849 2005-06-19 18:36 01 - Soothe.mp3
  -rw-r--r-- 1 mythtv mythtv  3197934 2005-06-19 18:36 02 - Frail And Bedazzled.mp3
  -rw-r--r-- 1 mythtv mythtv  3467503 2005-06-19 18:36 03 - Plum.mp3
  -rw-r--r-- 1 mythtv mythtv  4003745 2005-06-19 18:36 04 - Whir.mp3
  -rw-r--r-- 1 mythtv mythtv  3414845 2005-06-19 18:36 05 - Blew Away.mp3
  -rw-r--r-- 1 mythtv mythtv  2413413 2005-06-19 18:36 06 - Pissant.mp3
  -rw-r--r-- 1 mythtv mythtv  4348572 2005-06-19 18:36 07 - Hello Kitty Kat.mp3
  -rw-r--r-- 1 mythtv mythtv  5132656 2005-06-19 18:36 08 - Obscured.mp3
  -rw-r--r-- 1 mythtv mythtv  3099704 2005-06-19 18:36 09 - Landslide.mp3
  -rw-r--r-- 1 mythtv mythtv 10542719 2005-06-19 18:36 10 - Starla.mp3
  -rw-r--r-- 1 mythtv mythtv  3210041 2005-06-19 18:36 11 - Blue.mp3
  -rw-r--r-- 1 mythtv mythtv  3449127 2005-06-19 18:36 12 - Girl Named Sandoz.mp3
  -rw-r--r-- 1 mythtv mythtv  4098213 2005-06-19 18:36 13 - La Dolly Vita.mp3
  -rw-r--r-- 1 mythtv mythtv  2337344 2005-06-19 18:36 14 - Spaced.mp3
  
  dave@bender:~/mp3fs$ id3info /mnt/mp3/Smashing\ Pumpkins/Pisces\ Iscariot/01\ -\ Soothe.mp3

  *** Tag information for /mnt/mp3/Smashing Pumpkins/Pisces Iscariot/01 - Soothe.mp3
  === TSSE (Software/Hardware and settings used for encoding): LAME v3.96.1
  === TIT2 (Title/songname/content description): Soothe
  === TPE1 (Lead performer(s)/Soloist(s)): Smashing Pumpkins
  === TALB (Album/Movie/Show title): Pisces Iscariot
  === TRCK (Track number/Position in set): 1
  *** mp3 info
  MPEG1/layer III
  Bitrate: 128KBps
  Frequency: 44KHz
  
  dave@bender:~/mp3fs$ time cp /mnt/mp3/Smashing\ Pumpkins/Pisces\ Iscariot/01\ -\ Soothe.mp3 /tmp/
  
  real    0m12.917s
  user    0m0.004s
  sys     0m0.020s
  
  dave@bender:~/mp3fs$ xmms /mnt/mp3/Smashing\ Pumpkins/Pisces\ Iscariot/* &


Download
--------

Releases are made through the sourceforge files page:

  https://sourceforge.net/projects/mp3fs/files/

Development
-----------

MP3FS uses Git for revision control. You can obtain the full repository
with::

  git clone git://mp3fs.git.sourceforge.net/gitroot/mp3fs/mp3fs

MP3FS is written in C and requires the following libraries:

- fuse (>= 2.6.0)
- flac (>= 1.1.4 unless using MP3FS 0.1x)
- lame
- libid3tag

Additionally, MP3FS includes GPL'd code from a number of other projects:

- `Talloc <http://talloc.samba.org/>`_, a heirachical memory allocator
  from Samba
- A class implementation in C from `PyFlag <http://www.pyflag.net/>`_

License
-------

This program can be distributed under the terms of the GNU GPL version 3
or later. You can find it `online
<http://www.gnu.org/licenses/gpl-3.0.html>`_ or in the MP3FS distribution
in the COPYING file.
