#! /bin/sh

# vcp.sh - VPATH enabled variant of cp
# (C) 2001 by Matthias Andree <matthias.andree@gmx.de>

#    This library is free software; you can redistribute it and/or
#    modify it under the terms of the GNU Lesser General Public
#    License as published by the Free Software Foundation; either
#    version 2 of the License, or (at your option) any later version.

#    This library is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#    Lesser General Public License for more details.

#    You should have received a copy of the GNU Lesser General Public
#    License along with this library; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# $Id: vcp.sh,v 1.2 2001/11/29 17:48:30 emma Exp $

# Usage: vcp.sh VPATH file [...] dir
# VPATH is specified as d1:d2:d3

set +e
test=test
if ( $test -e $0 ) 2>/dev/null ; then : ; else test=/usr/bin/test ; fi
if ( $test -e $0 ) 2>/dev/null ; then : ; else
    echo "cannot find a working test command."
    exit 1 
fi
set -e

find_vpath() {
# find $1 in PATH-style $VPATH
# echo if found
# echo nothing if not found
  (
    r=$1
    IFS=":"
    for p in $VPATH ; do
      if $test -e "$p/$r" ; then 
	echo "$p/$r"
	break
      fi
    done
  )
}

usage() { # never returns, calls exit
    echo "*** vcp.sh (C) 2001 by Matthias Andree ***"
    echo "usage: $0 [vcp options] VPATH file1 [file2 [...]] dir"
    echo "to pass options through cp without parsing, use --pass=option"
    echo "vcp options:"
    echo "  -h, --help, -?: show this help text"
    echo "  -v: be verbose (also pass to cp)"
    exit $1
}

# strip options, pass on to cp later
opts=""
verbose=0
while [ "$1" ] ; do
    case "$1" in 
    --help|-h|-\?)
	usage 0
	;;
    --pass=*)
        opts="${opts}${opts:+ }${1#--pass=}"
	shift
	;;
    --)
	shift
	break
	;;
    -*)
	if [ "$1" = "-v" ] ; then verbose=1 ; fi
	opts="${opts}${opts:+ }$1"
	shift
	;;
    *)
	break
	;;
    esac
done	

if [ $# -lt 3 ] ; then usage 1 ; fi
VPATH=$1
shift

i=""
while [ $# -gt 1 ] ; do
  j=`find_vpath "$1"`
  e="$1 not found in $VPATH"
  j=${j:?"$1 not found in $VPATH"}
  if [ $verbose -gt 0 ] ; then echo "found $1 in ${j%$1}" ; fi
  i="${i}${i:+ }\"${j}\""
  shift
done

IFS=:
eval cp $opts -- $i $1
