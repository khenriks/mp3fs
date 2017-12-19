# Process this file with autoconf to produce a configure script.

# Sets up package and initializes build system.
AC_INIT([FFMPEGFS], [0.94])
AC_CONFIG_SRCDIR([src/ffmpegfs.c])
AC_CONFIG_AUX_DIR([config])
AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Optimisation
CXXFLAGS="-O3 -g"
CFLAGS="-O3 -g"

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_RANLIB

# Check integer size
AC_CHECK_SIZEOF(int)

AS_IF([test "$ac_cv_sizeof_int" -lt 4],
    AC_MSG_ERROR([Integer type 'int' must be at least 4 bytes in size.]))

# FFMPEG support checks
AC_ARG_WITH([ffmpeg],
    [AS_HELP_STRING([--without-ffmpeg],
        [disable support for FFMPEG libraries])],
    [], [with_ffmpeg=yes])

# Tested with...
#
# libavutil      55. 34.101		54.3.0 (required)
# libavcodec     57. 64.101		56.1.0 (required)
# libavformat    57. 56.101		56.1.0 (required)
# libavdevice    57.  6.100
# libavfilter     6. 82.100
# libavresample   3.  5.  0		2.1.0 (required)
# libswscale      4.  2.100		3.0.0 (required)
# libswresample   2.  3.100
# libpostproc    54.  5.10
#
# May work with older versions but this is not guaranteed.

AS_IF([test "x$with_ffmpeg" != xno],
    [PKG_CHECK_MODULES([libavutil], [libavutil >= 54.3.0],
		[PKG_CHECK_MODULES([libavcodec], [libavcodec >= 56.1.0],
			[PKG_CHECK_MODULES([libavformat], [libavformat >= 56.1.0],
				[PKG_CHECK_MODULES([libavresample], [libavresample >= 2.1.0],
						[PKG_CHECK_MODULES([libswscale], [libswscale >= 3.0.0],
							[AC_DEFINE([HAVE_FFMPEG], [1], [Use FFMPEG libraries.])]
						)
						]
				)
				]
			)
			]
        )
		]
	)
	]
)

AM_CONDITIONAL([HAVE_FFMPEG], [test "x$with_ffmpeg" != xno])

# Error checks
AS_IF([test "x$with_flac" = no],
    AC_MSG_ERROR([FFmpeg library not enabled or not found. Please make sure its development modules are installed.]))

# Checks for sqlite3
PKG_CHECK_MODULES([sqlite3], [sqlite3 >= 3.7.13])
AC_SEARCH_LIBS([sqlite3_errstr], [sqlite3], [AC_DEFINE([HAVE_SQLITE_ERRSTR], [1], [libsqlite3 has sqlite3_errstr() function.])], [])
AC_SEARCH_LIBS([sqlite3_db_cacheflush], [sqlite3], [AC_DEFINE([HAVE_SQLITE_CACHEFLUSH], [1], [libsqlite3 has sqlite3_db_cacheflush() function.])], [])

# Checks for packages which use pkg-config.
PKG_CHECK_MODULES([fuse], [fuse >= 2.6.0])

# Large file support
AC_SYS_LARGEFILE

# Define POSIX standard conformance
AC_DEFINE([_POSIX_C_SOURCE], [200809L], [Define the POSIX version])

# Outputs resulting files.
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 test/Makefile])
AC_OUTPUT
