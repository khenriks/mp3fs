PATH=$PWD/../src:$PATH

function setup {
    DIRNAME="$(mktemp -d)"
    mp3fs -d "$PWD/flac" "$DIRNAME" 2>$0-debug.log &
	sleep 0.1
}

function finish {
    hash fusermount 2>&- && fusermount -u "$DIRNAME" || umount "$DIRNAME"
    rmdir "$DIRNAME"
}
