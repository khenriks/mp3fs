#! /bin/sh

libtoolize --force
aclocal-1.9 -I config
automake-1.9 --add-missing --copy
autoconf
