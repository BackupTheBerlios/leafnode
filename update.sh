#!/bin/sh
# This script updates leafnode installations < 1.6

set -e
if [ $# -ne 5 ]
then
    echo usage: $0 [spooldir] [libdir] [lockfile] [runas_user] [runas_group]
    echo This script should preferentially be called from the Makefile only.
    exit 1
fi

SPOOLDIR=$1
LIBDIR=$2
LOCKFILE=$3
RUNAS_USER=$4
RUNAS_GROUP=`id -g $4`
SRCDIR=`pwd`

if [ `id -u` -ne 0 ]
then
    echo This command must be run as root.
    exit 1
fi

if [ ! -d ${SPOOLDIR}/leaf.node ]
then
    echo To update your old news installation, you must first run "make install".
    exit 1
fi

if [ -f ${LOCKFILE} ]
then
    echo Lockfile ${LOCKFILE} exists - re-run \"make update\" later
    exit 1;
fi

touch ${LOCKFILE}

if [ -f ${LIBDIR}/groupinfo ]
then
    if [ -f ${SPOOLDIR}/leaf.node/groupinfo ]
    then
    	echo Re-sorting groupinfo file...
	mv ${SPOOLDIR}/leaf.node/groupinfo ${SPOOLDIR}/leaf.node/groupinfo.old
	sort -f < ${SPOOLDIR}/leaf.node/groupinfo.old > ${SPOOLDIR}/leaf.node/groupinfo
	rm ${LOCKFILE}
	echo Done.
	exit 0;
    fi

    echo Creating new file for main server ...
    server=`awk '{ if ($1 == "server") printf("%s\n", substr($0,index($0,"=")+1)); }' < ${LIBDIR}/config | tr -d '	 '`
    cd ${SPOOLDIR}/interesting.groups
    ls -c1 | xargs -i@@ grep @@\  ${LIBDIR}/groupinfo | \
	cut -d\  -f 1,4 > ${SPOOLDIR}/leaf.node/$server
    echo Converting groupinfo file ... your old groupinfo file will be in ${SPOOLDIR}/leaf.node/groupinfo.old
    awk '{ printf("%s %d %d 0", $1, $2, $3); for (i = 5; i <= NF; i++) printf(" %s", $i); printf("\n"); }' < ${LIBDIR}/groupinfo | sort -f > ${SPOOLDIR}/leaf.node/groupinfo
    mv ${SPOOLDIR}/leaf.node/groupinfo ${SPOOLDIR}/leaf.node/groupinfo.old
    ${SRCDIR}/lsort > ${SPOOLDIR}/leaf.node/groupinfo
    mv ${LIBDIR}/groupinfo ${SPOOLDIR}/leaf.node/groupinfo.old
    echo Move other files ...
    find ${LIBDIR} -type f -not -name 'config*' -exec mv '{}' ${SPOOLDIR}/leaf.node/ \;
    chown $RUNAS_USER:$RUNAS_GROUP ${SPOOLDIR}/leaf.node/*
    chmod 664 ${SPOOLDIR}/leaf.node/*
    echo Done.
    rm ${LOCKFILE}
    exit 0
fi

echo Re-sorting groupinfo file...
mv ${SPOOLDIR}/leaf.node/groupinfo ${SPOOLDIR}/leaf.node/groupinfo.old
${SRCDIR}/lsort > ${SPOOLDIR}/leaf.node/groupinfo
echo Done.
rm ${LOCKFILE}
chown $RUNAS_USER.$RUNAS_GROUP ${SPOOLDIR}/leaf.node/groupinfo
exit 0
