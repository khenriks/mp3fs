# vim: ft=bash

PATH=$PWD/../src:$PATH
export LC_ALL=C

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
NC=$'\033[0m'  # No Color

check_equal () {
    echo -n "Testing \"$1\" = \"$2\": "
    if [ "$1" = "$2" ]; then
        echo "${GREEN}OK!${NC}"
    else
        echo "${RED}BAD${NC}"
        return 1
    fi
}

cleanup () {
    EXIT=$?
    # Errors are no longer fatal
    set +e
    hash fusermount 2>&- && fusermount -u "$DIRNAME" || umount "$DIRNAME"
    rmdir "$DIRNAME"
    exit $EXIT
}

mp3fserr () {
    exit 99
}

set -e
trap cleanup EXIT
trap mp3fserr USR1

SRCDIR="$( cd "${BASH_SOURCE%/*}/srcdir" && pwd )"
DIRNAME="$(mktemp -d)"
( mp3fs -d "$SRCDIR" "$DIRNAME" --logfile="$0.builtin.log" || kill -USR1 $$ ) &
while ! mount | grep -q "$DIRNAME" ; do
    sleep 0.1
done
