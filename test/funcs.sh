PATH=$PWD/../src:$PATH
export LC_ALL=C

cleanup () {
    EXIT=$?
    # Errors are no longer fatal
    set +e
    hash fusermount 2>&- && fusermount -u "$DIRNAME" || umount "$DIRNAME"
    rmdir "$DIRNAME"
    exit $EXIT
}

ffmpegfserr () {
    exit 99
}

set -e
trap cleanup EXIT
trap ffmpegfserr USR1

SRCDIR="$( cd "${BASH_SOURCE%/*}/srcdir" && pwd )"
DIRNAME="$(mktemp -d)"
( ffmpegfs -d "$SRCDIR" "$DIRNAME" --logfile=$0.builtin.log || kill -USR1 $$ ) &
while ! mount | grep -q "$DIRNAME" ; do
    sleep 0.1
done
