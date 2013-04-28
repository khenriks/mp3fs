# Process this file with autoconf to produce a configure script.

# Sets up package and initializes build system.
AC_INIT([MP3FS], [0.31])
AC_CONFIG_SRCDIR([src/mp3fs.c])
AC_CONFIG_AUX_DIR([config])
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX

# Check integer size
AC_CHECK_SIZEOF(int)

AS_IF([test "$ac_cv_sizeof_int" -lt 4],
    AC_MSG_ERROR([Integer type 'int' must be at least 4 bytes in size.]))

# FLAC support checks
AC_ARG_WITH([flac],
    [AS_HELP_STRING([--without-flac],
        [disable support for FLAC decoding])],
    [], [with_flac=yes])

AS_IF([test "x$with_flac" != xno ],
    [PKG_CHECK_MODULES([flac], [flac++ >= 1.1.4],
        [AC_DEFINE([HAVE_FLAC], [1], [Use FLAC library.])])])

AM_CONDITIONAL([HAVE_FLAC], [test "x$with_flac" != xno])

# MP3 support checks
AC_ARG_WITH([mp3],
    [AS_HELP_STRING([--without-mp3],
        [disable support for MP3 encoding])],
    [], [with_mp3=yes])

AS_IF([test "x$with_mp3" != xno ],
    [PKG_CHECK_MODULES([id3tag], [id3tag])
     AC_CHECK_LIB([mp3lame], [lame_init],, [AC_MSG_ERROR([You must have liblame-dev installed to build mp3fs.])])
     AC_CHECK_HEADER([lame/lame.h],, [AC_MSG_ERROR([You must have liblame-dev installed to build mp3fs.])])
     AC_DEFINE([HAVE_MP3], [1], [Use LAME and id3tag libraries.])])

AM_CONDITIONAL([HAVE_MP3], [test "x$with_mp3" != xno])

# Checks for packages which use pkg-config.
PKG_CHECK_MODULES([fuse], [fuse >= 2.6.0])

# Large file support
AC_SYS_LARGEFILE

# Outputs resulting files.
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])
AC_OUTPUT
