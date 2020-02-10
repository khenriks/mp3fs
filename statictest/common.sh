# vim: ft=bash
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'  # No Color

ROOT="$(cd "${BASH_SOURCE%/*}/.." && pwd)"
SOURCES=($(find "$ROOT" -path '*/src/lib' -prune -o \( -name '*.cc' -o \
           -name '*.h' -o -name '*.c' \) -printf '%P\n' | sort))

COMMONDIR="${BASH_SOURCE%/*}"

in_main () {
    [ "${BASH_SOURCE[1]}" = "$0" ]
}

driver () {
    check_func=$1
    bad_msg_func=$2
    bad_files=()
    for src in "${SOURCES[@]}" ; do
        if $check_func "$ROOT/$src" ; then
            echo -e "${GREEN}GOOD${NC}: $src"
        else
            echo -e "${RED}BAD${NC} : $src"
            bad_files+=("$ROOT/$src")
        fi
    done
    if [ ${#bad_files[@]} -ne 0 ] ; then
        echo
        $bad_msg_func "${bad_files[*]}"
        return 1
    fi
}

multidriver () {
    tabular_multidriver "$@"
}

tabular_multidriver () {
    check_names=("${@%,*}")
    check_funcs=("${@#*,}")

    longest_src=0
    for src in "${SOURCES[@]}" ; do
        if [ ${#src} -gt $longest_src ]; then
            longest_src=${#src}
        fi
    done
    firstcol=$(( longest_src + 6 ))

    cols=()
    printf "%-${firstcol}s"
    for name in "${check_names[@]}" ; do
        echo -n "  [$name]"
        cols+=($(( ${#name} + 2 )))
    done
    echo
    
    everygood=y
    for src in "${SOURCES[@]}" ; do
        printf "%-${firstcol}s" "    : $src"
        allgood=y
        for i in "${!check_funcs[@]}" ; do
            func="${check_funcs[$i]}"
            if $func "$ROOT/$src" >/dev/null 2>&1 ; then
                printf "  ${GREEN}%-${cols[$i]}s${NC}" " OK"
            else
                printf "  ${RED}%-${cols[$i]}s${NC}" " NO"
                allgood=n
                everygood=n
            fi
        done
        if [ $allgood = y ] ; then
            echo -e "\r${GREEN}GOOD${NC}"
        else
            echo -e "\r${RED}BAD${NC}"
        fi
    done
    if [ $everygood != y ] ; then
        echo
        echo "Run individual statictest/check_* scripts for steps to fix."
        return 1
    fi
}
