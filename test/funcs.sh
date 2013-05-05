PATH=$PWD/../src:$PATH

function setup {
    DIRNAME="$(mktemp -d)"
    mp3fs -d "$PWD/flac" "$DIRNAME" 2>$0-debug.log &
    while ! mount | grep -q "$DIRNAME" ; do
        sleep 0.1
    done
}

function finish {
    hash fusermount 2>&- && fusermount -u "$DIRNAME" || umount "$DIRNAME"
    rmdir "$DIRNAME"
}
