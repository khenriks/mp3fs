#! /bin/sh

libtoolize --force
aclocal -I config
automake --add-missing --copy
autoconf
