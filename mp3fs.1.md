---
adjusting: l
hyphenate: false
title: MP3FS(1) \| User Commands
---

# NAME

mp3fs - mounts and transcodes FLACs and OGGs to MP3s on the fly

# SYNOPSIS

**mp3fs** \[*OPTION*\]...\ *IN_DIR* *OUT_DIR*

# DESCRIPTION

The mp3fs(1) command will mount the directory *IN_DIR* on *OUT_DIR*.
Thereafter, accessing *OUT_DIR* will show the contents of *IN_DIR*, with all
FLAC/Ogg Vorbis files transparently renamed and transcoded to MP3 format upon
access.

# OPTIONS

**-b, -obitrate**=*RATE*

:   Set the bitrate to use for encoding. Acceptable values for *RATE* are any
    which are allowed by the MP3 format. According to the manual for LAME, this
    means:

    For sampling frequencies of 32, 44.1, and 48 kHz, *RATE* can be among 32,
    40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, and 320.

    For sampling frequencies of 16, 22.05, and 24 kHz, *RATE* can be among 8,
    16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, and 160.

    When in doubt, it is recommended to choose a bitrate among 96, 112, 128,
    160, 192, 224, 256, and 320. If not specified, *RATE* defaults to 128.

**-d, -odebug**

:   Enable debug output. This will result in a large quantity of diagnostic
    information being printed to stderr as the program runs. This option will
    normally not be used. It implies **-f**.

**-f**

:   Run in the foreground instead of detaching from the terminal.

**--gainmode, -ogainmode**=*MODE*

:   Set mode to use for interpreting ReplayGain tags. The allowed values for
    *MODE* are:

    0 - Ignore all ReplayGain tags.

    1 - Prefer album gain tag, but fall back to track gain if album gain is not
    present.

    2 - Use track gain if present.

**--gainref, -ogainref**=*REF*

:   Set the reference loudness in decibels to adjust ReplayGain tags.
    ReplayGain software usually defaults to a loudness of 89 dB, but if this is
    too quiet, a higher value can be set here, causing adjustment of ReplayGain
    values in tags.

**-h, --help**

:   Print usage information.

**--log_format, -olog_format**=*FORMAT*

:   Specify the format to use for log messages, with the following
    substitutions:

    %I - thread ID

    %L - log level

    %M - log message

    %T - time, formatted as YYYY-MM-DD HH:MM:SS

    The default is "\[%T\] tid=%I %L: %M".

**--log_maxlevel, -olog_maxlevel**=*LEVEL*

:   Set the maximum level of messages to log, either ERROR, INFO, or DEBUG.
    Defaults to INFO, and forced to DEBUG in debug mode. Note that this does
    not enable logging; other log flags must be set to specify where to log.

**--log_stderr, -olog_stderr**

:   Output logging messages to stderr. Enabled in debug mode.

**--log_syslog, -olog_syslog**

:   Output logging messages to the system log.

**--logfile, -ologfile**=*FILE*

:   Set the file to log to. By default, no log file will be written.

**--quality, -oquality**=*QUALITY*

:   Set quality for encoding, as understood by LAME. The slowest and best
    quality is 0, while 9 is the fastest and worst quality. The default value
    is 5, although according to the LAME manual, 2 is recommended.

**-s**

:   Force single-threaded operation.

**--statcachesize, -ostatcachesize**=*SIZE*

:   Set the number of cached stat entries to store. This is needed for
    reasonable performance when VBR is enabled. Each entry takes 100-200 bytes
    of memory. Entries are evicted from the cache in least recently used order.

**--vbr, -ovbr**

:   Use variable bit rate encoding. When enabled, the **-b** or **-obitrate**
    options set the maximum bit rate. If enabled, the **--statcachesize** or
    **-ostatcachesize** options are strongly recommended.

**-V, --version**

:   Output version information.

# COPYRIGHT

Copyright (C) 2006-2008 David Collett and 2008-2013 K.\ Henriksson. This is
free software: you are free to change and redistribute it under the terms of
the GNU General Public License (GPL) version 3 or later.

This manual is copyright (C) 2010-2020 K.\ Henriksson and may be distributed
under the GNU Free Documentation License (GFDL) 1.3 or later with no invariant
sections, or alternatively under the GNU General Public License (GPL) version 3
or later.
