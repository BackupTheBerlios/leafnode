#! /bin/sh

# primitive UUCP batch builder, adds #! rnews line, optionally
# compresses and prepends the proper unbatcher line.

# (C) 2002 by Matthias Andree
# Redistributable under the terms of the GNU General Public License v2.

# usage: batch [options] article [article [...]]
#
# options are:
# -z -> build "gunbatch" (gzip)
# -Z -> build "cunbatch" (compress)

# article arguments point to regular files of one complete RFC-1036
# article per file

set -e

compressor="cat -"
while [ "x$1" != "x" ]
do
    case $1 in
	-Z) prefix='#! cunbatch'
	    compressor="compress -b15"
	    ;;
	-z) prefix='#! gunbatch'
	    compressor="gzip -c -9"
	    ;;
	-*) echo "unknown option $1"
	    ;;
	*)
	    break;
	    ;;
    esac
    shift
done

if [ "x$prefix" != "x" ] ; then echo "$prefix" ; fi

(
    set -e
    while [ "x$1" != "x" ] ; do 
	find "$1" -printf "#! rnews %s\n" -prune 
	cat "$1"
	shift
    done
) | $compressor
