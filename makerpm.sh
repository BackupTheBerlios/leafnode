#!/bin/sh
# This script cares for the rpm crap.
# First draft written by Karsten Merker <merker@guug.de>
# Heavily modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
# Further modifications by K. Merker <merker@guug.de> 1999/12/11; some small
# bugfixes and addition of buildroot- and prefix-support
# More modifications for SuSE by Juergen Salk <juergen.salk@gmx.net>
# See README for restrictions of use of this software.

echo

# if [ `id -u` -ne 0 ]
# then
#    echo This command must be run as root.
#    exit 1
# fi

if [ $# -ne 10 ]
then
    echo "Usage: $0 [path to rpm] [leafnode version] [usrdir] [bindir] [mandir]"
    echo "  [lockfile] [spooldir] [path to awk] [libdir] [prefix]"  
    echo 'Possible reason for failure: rpm not found?'
    echo This script should preferentially be called from the Makefile only.
    exit 1
fi

RPM=$1
VERSION=$2
USRDIR=$3
BINDIR=$4
MANDIR=$5
LOCKFILE=$6
LOCKDIR=`dirname ${LOCKFILE}`
SPOOLDIR=$7
AWK=$8
LIBDIR=$9
shift 1
PREFIX=$9

HOMEDIR=`/bin/pwd`

# export RPM=/bin/rpm-2.5.5

RPMVERSION=`${RPM} --version | ${AWK} '{ if ($3 < 3 ) print("2"); else print("3"); }'`
if [ ${RPMVERSION} -eq 2 ]
then
  echo rpm version 2.x recognized
  RPMSRCDIR=`$RPM --showrc | grep ^sourcedir | ${AWK} '{ print($3); }'`
  SPECDIR=`$RPM --showrc | grep ^specdir | $AWK '{ print($3); }'`
  BUILDDIR=`$RPM --showrc | grep ^builddir | $AWK '{ print($3); }'`
else
  echo rpm version 3.x recognized -- have to make educated guesses
  if [ -f /etc/rc.config ]; then
# seems to be a SuSE system
   DISTRIBUTION="SuSE > 6.0" 
   RPMSRCDIR=/usr/src/packages/SOURCES
   SPECDIR=/usr/src/packages/SPECS
   BUILDDIR=/usr/src/packages/BUILD
  fi
  if [ -f /etc/rc.d/init.d/functions ]; then
# seems to be a RedHat system
   DISTRIBUTION="RedHat > 5.0"
   RPMSRCDIR=/usr/src/redhat/SOURCES
   SPECDIR=/usr/src/redhat/SPECS
   BUILDDIR=/usr/src/redhat/BUILD
  fi
fi
SPECFILE=${SPECDIR}/leafnode-${VERSION}.spec
LISTFILE=${RPMSRCDIR}/leafnode-${VERSION}.filelist

if [ ! -x ${RPM} ]
then
    echo rpm should be in $RPM but not found
    exit 1
fi

echo Creating ${LISTFILE} ...

mkdir -p ${RPMSRCDIR}

cat << EOF > $LISTFILE
%doc CHANGES
%doc CREDITS
%doc FAQ
%doc INSTALL
%doc README
%doc TODO
%doc update.sh
${BINDIR}/leafnode
${BINDIR}/fetchnews
${BINDIR}/texpire
${BINDIR}/checkgroups
${BINDIR}/applyfilter
${USRDIR}/newsq
${MANDIR}/man1/newsq.1
${MANDIR}/man8/checkgroups.8
${MANDIR}/man8/fetchnews.8
${MANDIR}/man8/leafnode.8
${MANDIR}/man8/texpire.8
${LIBDIR}/config.example
%dir ${LOCKDIR}
%dir ${SPOOLDIR}
%dir ${SPOOLDIR}/leaf.node
%dir ${SPOOLDIR}/message.id
%dir ${SPOOLDIR}/interesting.groups
%dir ${SPOOLDIR}/out.going
EOF

for a in  0 1 2 3 4 5 6 7 8 9 ; do \
for b in 0 1 2 3 4 5 6 7 8 9 ; do \
for c in 0 1 2 3 4 5 6 7 8 9 ; do \
echo %dir ${SPOOLDIR}/message.id/${a}${b}${c} >> $LISTFILE
done ; done ; done

echo Creating ${SPECFILE} ...

mkdir -p `dirname ${SPECFILE}`

cat << EOF > ${SPECFILE}
Summary: Leafnode - a leafsite NNTP server (Version ${VERSION})
Summary(de): Leafnode - ein offline-Newsserver (Version ${VERSION})
Name: leafnode
Version: ${VERSION}
Release: 1
Copyright: see README
Group: Applications/News
URL: http://www.leafnode.org/
Source: ftp://wpxx02.toxi.uni-wuerzburg.de/pub/leafnode-${VERSION}.tar.gz
Source1: ${LISTFILE}
Distribution: ${DISTRIBUTION}
Vendor: none
Buildroot: /var/tmp/leafnode-${VERSION}-buildroot/
Packager: rpm template specfile written by K. Merker <merker@guug.de>
%description
Leafnode is a small NNTP server for leaf sites without permanent
connection to the internet. It supports a subset of NNTP and is able to
automatically fetch the newsgroups the user reads regularly from the
newsserver of the ISP.

%description -l de
Leafnode ist ein offline-Newsserver, der vor allem für den typischen
Einzelnutzer-Rechner ohne permanente Internetanbindung geeignet ist.
Leafnode bezieht automatisch die Newsgroups, die der oder die Nutzer
regelmaessig lesen, vom Newsserver des Providers.

%prep
%setup
%build
./configure  --prefix=$PREFIX
make

%install
make INSTALLROOT=\$RPM_BUILD_ROOT install

%clean
# do a cleanup manually

%files -f ${RPMSRCDIR}/leafnode-${VERSION}.filelist
EOF


cp ${HOMEDIR}/leafnode-pcre-${VERSION}.tar.gz ${RPMSRCDIR}/leafnode-pcre-${VERSION}.tar.gz

mkdir -p ${BUILDDIR}

echo Building RPMs from ${SPECFILE}:
${RPM} -ba ${SPECFILE}
