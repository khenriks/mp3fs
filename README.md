mp3fs
=====

Web site:
Original Version: http://khenriks.github.io/mp3fs/
FFMPEG Version: https://github.com/nschlia/mp3fs

NOTE THAT THIS IS AN ALPHA VERSION FOR TESTING ONLY!

mp3fs is a read-only FUSE filesystem which transcodes between audio 
and video formats (many formats that FFMPEG can decode to MP3 or MP4) 
on the fly when opened and read.

This can let you use a multi media file collection with software 
and/or hardware which only understands the MP3 or MP4 format, or 
transcode files  through simple drag-and-drop in a file browser.

For installation instructions see the [install](INSTALL.md) file.

RESTRICTIONS:

* mp4 support is highly experimental.
* Cover arts are not yet supported.
* The current version is in alpha state and input is limited to:
  avi, flac, flv, m2ts, mkv, mov, mpg, oga, ogg, ogv, rm, ts, vob, 
  webm, wma and wmv.

Supported Linux Distributions
-----------------------------

**Suse** does not provide proprietary formats like AAC and H264, thus
the distribution FFMPEG is crippled. mp3fs will not be able to encode 
to H264 and AAC. End of story. 
See https://en.opensuse.org/Restricted_formats.

**Debian 8** comes with LIBAV 11 clone of FFMPEG. 

mp3fs compiles with LIBAV 11 and 12, but streaming directly while 
transcoding does not work. The first time a file is accessed playback 
will fail. After it has been decoded fully to cache playback does work. 
Playing the file via http may fail or it may take quite long until the
file starts playing. This is a LIBAV insufficiency. You may have to 
replace it with FFMPEG.

**Debian 9**, **Ubuntu 16** and **Ubuntu 17** include a decently recent
version of the original FFMPEG library.

Tested with:

* `Debian 8` **AVLib 11.11-1~deb8u1**: not working with LIBAV
* `Debian 9` **FFmpeg 3.2.8-1~deb9u1**: OK!
* `Ubuntu 16.04.3 LTS` **FFmpeg 2.8.11-0ubuntu0.16.04.1**: OK!
* `Ubuntu 17.10` **FFmpeg 3.3.4-2**: OK!
* `Suse 42` **FFmpeg 3.3.4**: No H264/AAC support by default

**Tips on other OSs and distributions like Mac or Red-Hat are welcome.**

Usage
-----

Mount your filesystem like this:

    mp3fs [-b bitrate] musicdir mountpoint [-o fuse_options]

For example,

    mp3fs -b 128 /mnt/music /mnt/mp3fs -o allow_other,ro

In recent versions of FUSE and mp3fs, the same can be achieved with the
following entry in `/etc/fstab`:

    mp3fs#/mnt/music /mnt/mp3fs fuse allow_other,ro,bitrate=128 0 0

At this point files like `/mnt/music/**.flac` and `/mnt/music/**.ogg` will
show up as `/mnt/mp3fs/**.mp4`.

Note that the "allow_other" option by default can only be used by root.
You must either run mp3fs as root or better add a "user_allow_other" key 
to /etc/fuse.conf.

"allow_other" is required to allow any user access to the mount, by
default this is only possible for the user who launched mp3fs.

How it Works
------------

When a file is opened, the decoder and encoder are initialised and
the file metadata is read. At this time the final filesize can be
determined approximately. This works well for mp3 output files,
but only fair to good for mp4.

As the file is read, it is transcoded into an internal per-file
buffer. This buffer continues to grow while the file is being read
until the whole file is transcoded in memory. Once decoded the 
file is kept in a disk buffer and can be accessed very fast.

Transcoding is done in an extra thread, so if other processes should
access the same file they will share the same transcoded data, saving
CPU time. If the first process abandons the file before its end,
transconding will continue for some time. If the file is accessed
again before the timeout transcoding will go on, if not stop and the
chunk created so far discarded to save CPU time.

Seeking within a file will cause the file to be transcoded up to the
seek point (if not already done). This is not usually a problem
since most programs will read a file from start to finish. Future
enhancements may provide true random seeking.

ID3 version 2.4 and 1.1 tags are created from the comments in the 
source file. They are located at  the start and end of the file 
respectively.

MP3 target only: A special optimisation is made so that applications 
which scan for id3v1 tags do not have to wait for the whole file to be 
transcoded before reading the tag. This *dramatically* speeds up such
applications.

Supported Output Formats
------------------------

A few words to the supported output formats which are mp3 and mp4 
currently. There is not much to say about the mp3 output as these 
are regular mp3 files with no strings attached. They should play 
well in any modern player.

The mp4 files created are special, though, as mp4 is not quite suited
for live streaming. Reason being that the start block of an mp4 
contains a field with the size of the compressed data section. Suffice
to say that this field cannot be filled in until the size is known,
which means that compression must be completed first. 

Alas, for a continous live stream, that size will never be known or
for our transcoded files one would have to wait for the whole file
to be recoded. If that was not enough some important pieces of 
information are located at the end of the file, including meta tags
with artist, album, etc.

To get around the restriction several extensions have been developed,
one of which is called "faststart" that relocates the afformentioned
data from the end to the beginning of the mp4. Additonally, the size field 
can be left empty (0). isml (smooth live streaming) is another extension.

For direct to stream transcoding several new features in mp4 need to
be used (ISMV, faststart, separate_moof/empty_moov to name them) 
which are not implemented in older versions (or if available, not 
working properly). 

By default faststart files will be created with an empty size field so 
that the file can be started to be written out at once instead of 
decoding it as a whole before this is possible. That would mean it would 
take some time before playback can start.

The data part is divided into chunks of about 1 second length each, 
this allowing to fill in the size fields early enough.

As a draw back not all players support the format, or play back with 
strange side effects. VLC plays the file, but updates the time display 
every second only. When streamed over HTML5 video tags, there will be no
total time shown, but that is OK, as it is yet unknown. The playback
cannot be positioned past the current playback position, only backwards.

But that's the price of starting playback, fast.

So there is a lot of work to be put into mp4 support, still.

The output format must be selectable for the desired audience, for
streaming or opening the files locally, for example.

Development
-----------

mp3fs uses Git for revision control. You can obtain the full repository
with:

    git clone https://github.com/khenriks/mp3fs.git (original version)
    git clone https://github.com/nschlia/mp3fs.git (this FFMPEG enabled version)

mp3fs is written in a mixture of C and C++ and uses the following libraries:

* [FUSE](http://fuse.sourceforge.net/)

If using the FFMPEG support (Libav works as well, but FFMPEG is recommended):

* [FFMPEG](https://www.ffmpeg.org/) or [LIBAV](https://www.libav.org/)

Authors
-------

This fork with FFMPEG support is maintained by Norbert Schlia 
(nschlia@oblivion-software.de) since 2017.

Based on work by K. Henriksson (from 2008 to 2017) and the original author 
David Collett (from 2006 to 2008).

Much thanks to them for the original work!

License
-------

This program can be distributed under the terms of the GNU GPL version 3
or later. It can be found [online](http://www.gnu.org/licenses/gpl-3.0.html)
or in the COPYING file.

This file and other documentation files can be distributed under the terms of
the GNU Free Documentation License 1.3 or later. It can be found
[online](http://www.gnu.org/licenses/fdl-1.3.html) or in the COPYING.DOC file.

FFmpeg License
--------------

FFmpeg is licensed under the GNU Lesser General Public License (LGPL) 
version 2.1 or later. However, FFmpeg incorporates several optional 
parts and optimizations that are covered by the GNU General Public 
License (GPL) version 2 or later. If those parts get used the GPL 
applies to all of FFmpeg. 

See https://www.ffmpeg.org/legal.html for details.
