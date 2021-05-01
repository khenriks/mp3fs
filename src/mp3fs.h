/*
 * Global program parameters for mp3fs
 *
 * Copyright (C) 2006-2008 David Collett
 * Copyright (C) 2008-2013 K. Henriksson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef MP3FS_MP3FS_H_
#define MP3FS_MP3FS_H_

/* Global program parameters */
struct Mp3fsParams {
    const char* basepath;
    int bitrate;
    int debug;
    const char* desttype;
    int gainmode;
    float gainref;
    const char* log_format;
    const char* log_maxlevel;
    int log_stderr;
    int log_syslog;
    const char* logfile;
    int quality;
    unsigned int statcachesize;
    int vbr;
};

extern Mp3fsParams params;

#endif  // MP3FS_MP3FS_H_
