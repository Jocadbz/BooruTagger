#!/bin/sh
set -eu

if [ "$#" -lt 2 ]; then
    echo 'usage: distcc-launcher.sh <distcc> <compiler> [arguments...]' >&2
    exit 2
fi

launcher=$1
compiler=${2##*/}
shift 2
exec "$launcher" "$compiler" "$@"
