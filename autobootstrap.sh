#! /bin/sh
# This file has been written by Matthias Andree and is public domain.
# All warranties are disclaimed. Use at your own risk.

set -e
aclocal
autoheader
automake -a
autoconf
