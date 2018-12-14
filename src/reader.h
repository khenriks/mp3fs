/*
 * Generic reader interface for MP3FS
 *
 * Copyright (C) 2018 K. Henriksson
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

#ifndef MP3FS_READER_H
#define MP3FS_READER_H

#include <unistd.h>

class Reader {
public:
    /** Read bytes into the internal buffer and into the given buffer. */
    virtual ssize_t read(char* buff, off_t offset, size_t len) = 0;

    virtual ~Reader() {}
};

class FileReader : public Reader {
public:
    FileReader(int fd) : fd_(fd) {}

    ~FileReader() override { close(fd_); }

    ssize_t read(char* buff, off_t offset, size_t len) override {
        return pread(fd_, buff, len, offset);
    }

private:
    int fd_;
};

#endif  // MP3FS_READER_H
