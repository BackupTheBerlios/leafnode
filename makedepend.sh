#! /bin/sh

# makedepend.sh
# poor man's makedepend, using gcc

# (C) 2001 by Matthias Andree

# arguments: 
# $1: Makefile to attach dependencies to
# $2...: Files to generate dependencies from

set -e
if test "x$1" = "x" ; then exit 1 ; fi

f=$1
shift
sed <"$f" >"${f}.mkdep" -e '/^# DO NOT DELETE/,$ d'
echo >>"${f}.mkdep" '# DO NOT DELETE'
echo >>"${f}.mkdep" ''
gcc -MM >>"${f}.mkdep" "$@"
mv -b "${f}.mkdep" "$f"
