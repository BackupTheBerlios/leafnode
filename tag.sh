#!/bin/sh
if [ $# -ne 1 ] ; then
    echo >&2 "Usage: $0 tagname"
    exit 1
fi
exec git tag -a -s -m "tagged $1" "$1"
