#!/bin/sh

################################
## Environment Setup ##
################################

# set CC/CXX/ETC. variables XXX a POSIX array
set -- CC clang CC gcc CC tcc CC cc
while [ "$#" -gt 2 ]; do
    if ! command -v $2 > /dev/null 2>&1; then
	shift 2
	continue
    fi

    first=$1
    twoth=$2

    eval ": \"\${$first:=$twoth}\"" # only set CC to clang and sundry if it is not already defined
    shift 2
done

################################
## Body ##
################################

set -- check.c input.c macro.c main.c make.c modtime.c rules.c target.c utils.c

tmp="$@"

while [ "$#" -gt 1 ]; do
    source="$1"
    object="${source%.c}.o"

    # build object code
    set -e
    $CC $CFLAGS $CPPFLAGS -c $source -o $object
    set +e

    shift
done

$CC $CFLAGS $LDFLAGS -o make $(printf "$tmp" | sed 's/\.c/\.o/')
