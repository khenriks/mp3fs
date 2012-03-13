#! /bin/sh

# We can just let autoreconf do what is needed
echo 'Running autoreconf --install'
autoreconf --install

# On a Mac with Homebrew, the following command is needed:
# autoreconf --install -I /usr/local/share/aclocal
# This will require autoconf from homebrew-alt
