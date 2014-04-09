---
layout: main
title: mp3fs
---

mp3fs is a read-only [FUSE](http://fusess.sourceforge.net/) filesystem which
transcodes between audio formats (currently FLAC to MP3) on the fly when files
are opened and read.

It can let you use a FLAC collection with software and/or hardware which
only understands the MP3 format, or transcode files through simple
drag-and-drop in a file browser.

Usage
-----

Mount your music like so (for example)

{% highlight console %}
$ mp3fs -b 192 /mnt/music /mnt/mp3 -o allow_other,ro
{% endhighlight %}

or use the following entry in `/etc/fstab`:

{% highlight text %}
mp3fs#/mnt/music /mnt/mp3 fuse allow_other,ro,bitrate=192 0 0
{% endhighlight %}

Here are the original files:

{% highlight console %}
$ ls -l /mnt/music/Machinarium
total 271252
-rw-r--r-- 1 khenriks khenriks 30661302 Feb 14  2012 1-01 The Bottom.flac
-rw-r--r-- 1 khenriks khenriks 22246923 Feb 14  2012 1-02 The Sea.flac
-rw-r--r-- 1 khenriks khenriks 20505800 Feb 14  2012 1-03 Clockwise Operetta.flac
-rw-r--r-- 1 khenriks khenriks 19319831 Feb 14  2012 1-04 Nanorobot Tune.flac
...
{% endhighlight %}

which now show up as mp3s on the mountpoint:

{% highlight console %}
$ ls -l /mnt/mp3/Machinarium
total 81030
-rw-r--r-- 1 khenriks khenriks  8607397 Feb 14  2012 1-01 The Bottom.mp3
-rw-r--r-- 1 khenriks khenriks  6295871 Feb 14  2012 1-02 The Sea.mp3
-rw-r--r-- 1 khenriks khenriks  6287732 Feb 14  2012 1-03 Clockwise Operetta.mp3
-rw-r--r-- 1 khenriks khenriks  5157984 Feb 14  2012 1-04 Nanorobot Tune.mp3
...
{% endhighlight %}

You can treat them just like regular files.

{% highlight console %}
$ mutagen-inspect /mnt/mp3/Machinarium/1-01\ The\ Bottom.mp3
-- /mnt/mp3/Machinarium/1-01 The Bottom.mp3
- MPEG 1 layer 3, 192000 bps, 44100 Hz, 330.32 seconds (audio/mp3)
APIC= (image/png, 679371 bytes)
TALB=Machinarium
TCON=Soundtrack
TDRC=2009-10-16
TIT2=The Bottom
TLEN=330279
TPE1=Tomáš Dvořák
TPE2=Tomáš Dvořák
TPOS=1/1
TRCK=1/14
TSSE=MP3FS

$ time cp /mnt/mp3/Machinarium/1-01\ The\ Bottom.mp3 /tmp

real    0m7.788s
user    0m0.000s
sys     0m0.012s
{% endhighlight %}

As far as your programs  are concerned, they are just regular files.

More Information
----------------

If you're interested to know more, check out the README or source on the
[GitHub page](https://github.com/khenriks/mp3fs). You can also browse the
<a href="mp3fs.1.html">manpage</a> on this site.
