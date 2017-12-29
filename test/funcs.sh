PATH=$PWD/../src:$PATH
export LC_ALL=C

cleanup () {
    EXIT=$?
    echo "Return code: $EXIT"
    # Errors are no longer fatal
    set +e
    # Unmount all
    hash fusermount 2>&- && fusermount -u "$DIRNAME" || umount "$DIRNAME"
    # Remove temporary directories
    rmdir "$DIRNAME"
    rm -Rf "$CACHEPATH"
    # Arrividerci
    exit $EXIT
}

ffmpegfserr () {
    echo "***TEST FAILED***"
    echo "Return code: 99"
    exit 99
}

set -e
trap cleanup EXIT
trap ffmpegfserr USR1

DESTTYPE=$1
SRCDIR="$( cd "${BASH_SOURCE%/*}/srcdir" && pwd )"
DIRNAME="$(mktemp -d)"
CACHEPATH="$(mktemp -d)"

#--disable_cache
( ffmpegfs -f "$SRCDIR" "$DIRNAME" --logfile=$0.$DESTTYPE.builtin.log --log_maxlevel=TRACE --cachepath="$CACHEPATH" --desttype=$DESTTYPE || kill -USR1 $$ ) &
while ! mount | grep -q "$DIRNAME" ; do
    sleep 0.1
done
