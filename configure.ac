dnl autoconf script for leafnode -*- m4 -*-
dnl written by Cornelius Krasel & Matthias Andree, 1999-2001
dnl written by Matthias Andree, 2002
dnl Process this file with autoconf to produce a configure script.
AC_INIT([leafnode.h])
AM_INIT_AUTOMAKE([leafnode],[2.0b8_ma9pre2])
AC_PREREQ(2.52)
AC_REVISION($Revision: 1.2 $)
AC_CONFIG_SRCDIR(leafnode.h)
AM_CONFIG_HEADER(config.h)
dnl AM_MAINTAINER_MODE

dnl Checks for programs.
AC_C_INLINE
AC_CHECK_TOOL([STRIP],[strip])
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_CHECK_PROG(AR,ar,ar)
AC_PATH_PROG(LYNX,lynx,no)
AC_PATH_PROG(RPM,rpm)
AC_PATH_PROG(CAT,cat,false)
AC_PATH_PROG(GZIP,gzip,false)
AC_PATH_PROG(BZIP2,bzip2,false)
AC_PATH_PROG(SED,sed,false)
AC_PATH_PROG(DEFAULTMTA,sendmail,/usr/sbin/sendmail,/usr/sbin:/usr/lib:/usr/etc)

dnl If we're using gcc, enable some warnings
AC_CACHE_CHECK(for additional compiler options, ac_cv_prog_gcc_flags, [
ac_cv_prog_gcc_flags=""
if test "$GCC" = yes
then
  echo "void dummy(void);" >configure-dummy.c
  echo "void dummy(void) {}" >>configure-dummy.c
  for i in -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Waggregate-return -Wmissing-declarations -Wmissing-format-attribute -Wnested-externs -Wunreachable-code -ggdb -fno-common ; do
    $CC $i $ac_cv_prog_gcc_flags -c configure-dummy.c >/dev/null 2>&1 && 
    ac_cv_prog_gcc_flags="$ac_cv_prog_gcc_flags $i"
  done
fi])
rm -f configure-dummy.c configure-dummy.o
GCC_FLAGS=$ac_cv_prog_gcc_flags
AC_SUBST(GCC_FLAGS)

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STAT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS([crypt.h errno.h fcntl.h limits.h sys/time.h syslog.h string.h strings.h unistd.h arpa/inet.h netdb.h netinet/in.h stddef.h sys/param.h sys/socket.h utime.h])

dnl Check whether all necessary include files are there
AC_CACHE_CHECK(for system include files, cf_cv_sysheaders, AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/param.h>],[
     return 0;],
   [cf_cv_sysheaders="yes"],
   [cf_cv_sysheaders="no"])
)
if test "$cf_cv_sysheaders" = "no"
then
  AC_MSG_ERROR(Kernel sources not installed - unable to continue)
fi

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_STRUCT_TM

dnl Check whether socklen_t is defined in /usr/include/sys/socket.h
AC_CACHE_CHECK(for socklen_t,cf_cv_socklen_t, AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/socket.h>],[
    socklen_t fodder;
    return fodder;],
   [cf_cv_socklen_t="yes"],
   [cf_cv_socklen_t="no"])
)
if test "$cf_cv_socklen_t" = "yes"
then
  AC_DEFINE(HAVE_SOCKLEN_T)
fi

dnl check whether to use dmalloc
AM_WITH_DMALLOC

dnl check where to put the spooldir
AC_MSG_CHECKING(spooldir)
AC_ARG_WITH(spooldir,
[  --with-spooldir         news spool directory (/var/spool/news)],
  if echo "$withval" | egrep >/dev/null -v '^/' ; then
    AC_MSG_ERROR(you must give an absolute path)
  fi
  withval=`echo "$withval" | sed 'sx/*$xx;'`
, 
  withval="/var/spool/news"
)
SPOOLDIR=$withval
AC_MSG_RESULT($SPOOLDIR)
AC_SUBST(SPOOLDIR)

dnl check where to put the confdir
if test "$sysconfdir" = '${prefix}/etc' \
  -a "(" "x$prefix" = xNONE -o "x$prefix" = "x/usr" ")" ; then
  CONFDIR='/etc/leafnode'
else
  CONFDIR="$sysconfdir"
fi
if test "$CONFDIR" != "$sysconfdir"; then
  AC_MSG_WARN([overriding sysconfdir to $CONFDIR])
  AC_MSG_WARN([use --sysconfdir to fix])
  AC_MSG_WARN([use --sysconfdir='\$prefix/etc' to get])
  AC_MSG_WARN([the default autoconf behaviour])
  sleep 2
fi
sysconfdir=$CONFDIR
AC_SUBST(sysconfdir)

dnl check where to put the lockfile
AC_MSG_CHECKING(lockfile)
AC_ARG_WITH(lockfile,
[  --with-lockfile         lockfile (/var/lock/news/fetchnews.lck)],
  if echo "$withval" | egrep >/dev/null -v '^/' ; then
    AC_MSG_ERROR(you must give an absolute path)
  fi
, 
  withval=/var/lock/news/fetchnews.lck  
)
LOCKFILE=$withval
AC_MSG_RESULT($LOCKFILE)
AC_SUBST(LOCKFILE)

dnl check for rpm build dir
dnl Check for rpm build dir
AC_MSG_CHECKING(rpm build dir)
d=/usr/src/packages
test -d ${d}/SPECS || d=/usr/src/redhat dnl RedHat
test -d ${d}/SPECS || d=none
RPMSRC=${d}
AC_MSG_RESULT(${RPMSRC})
test "${RMPSRC}" = "none" || AC_SUBST(RPMSRC)

dnl Check for libcrypt
AC_CHECK_LIB(crypt, crypt)

dnl Check for PCRE library.
AC_PATH_PROG(PCRECONFIG,pcre-config,no)
if test "$PCRECONFIG" != "no" ; then
  CF=`$PCRECONFIG --cflags`
  if echo "$CFLAGS" | grep >/dev/null 2>&1 "$CF" 
  then :
  else echo "adding $CF to CFLAGS" ; CFLAGS="$CFLAGS $CF" ; export CFLAGS
  fi
  LF=`$PCRECONFIG --libs`
  if echo "$LDFLAGS" | grep >/dev/null 2>&1 "$LF" 
  then :
  else echo "adding $LF to LDFLAGS" ; LDFLAGS="$LDFLAGS $LF" ; export LDFLAGS
  fi
fi

AC_CACHE_CHECK(for pcre.h,ac_cv_header_pcre_h,[
  AC_TRY_COMPILE([#include <pcre.h>],
	[return 0;],
	[ac_cv_header_pcre_h=yes],
	[ac_cv_header_pcre_h=no])
])

if test "$ac_cv_header_pcre_h" = yes
then
  # do not add this to LIBS (causes unnecessary linkage)
  AC_CHECK_LIB(pcre, pcre_compile, LINKPCRELIB="-lpcre")
fi

if test "$ac_cv_lib_pcre_pcre_compile" = yes
then
  AC_DEFINE(HAVE_LIBPCRE)
  AC_CACHE_CHECK(for new pcre_compile API,cf_cv_new_pcre_compile,[AC_TRY_COMPILE([
#include <pcre.h>],[
	char * pattern;
	int options;
	const char **errptr;
	int *erroffset;
	const unsigned char *tableptr;
	pcre_compile(pattern, options, errptr, erroffset, tableptr);],
  [cf_cv_new_pcre_compile="yes"],
  [cf_cv_new_pcre_compile="no"])
  ])
  if test $cf_cv_new_pcre_compile = "yes"
  then
    AC_DEFINE(NEW_PCRE_COMPILE)
  fi
  AC_CACHE_CHECK(for new pcre_exec API,cf_cv_new_pcre_exec,[AC_TRY_COMPILE([
#include <pcre.h>],[
	const pcre *pattern;
	const pcre_extra *extra;
	const char *subject;
	int length;
	int options;
	int *ovec;
	int ovecsize;
	pcre_exec(pattern, extra, subject, length, options, ovec, ovecsize);],
   [cf_cv_new_pcre_exec="no"], [ cf_cv_new_pcre_exec="yes" ])
 ])
  if test $cf_cv_new_pcre_exec = "yes"
  then
    AC_DEFINE(NEW_PCRE_EXEC)
  fi
fi

if test "$ac_cv_lib_pcre_pcre_compile" != "yes"
then
    echo "*** I cannot find PCRE. leafnode depends on it."
    echo "***"
    echo "*** If you have PCRE installed, pcre-config was not found."
    echo "*** You can work around this by adding the header location to"
    echo "*** the environment variables CPPFLAGS, i. e. CPPFLAGS=-I/opt/pcre/include"
    echo "*** and the library location to the environment variables LDFLAGS,"
    echo "*** e. g. LDFLAGS=-L/opt/pcre/lib, assuming your PCRE resides in /opt/pcre."
    echo "***"
    echo "*** If not you don't have PCRE installed, please download and install PCRE 3.7"
    echo "*** or a newer version from http://www.pcre.org/, it's easy:"
    echo "*** Just download, unpack, then cd to pcre-3.7, then"
    echo "*** ./configure && make, then as root: make install"
    echo "*** Then, reconfigure leafnode."
    exit 1
fi

AC_SUBST(PCRELIB)
AC_SUBST(LINKPCRELIB)

dnl Check for timezone and related stuff
AC_CACHE_CHECK(for gmtoff in struct tm,cf_cv_have_gmtoff,[AC_TRY_COMPILE([
#include <time.h>],[
	time_t now;
	long gmtoff;
	struct tm gmt;
	now = time(0);
	gmt = *(gmtime(&now));
	gmtoff = gmt.tm_gmtoff;],
   [cf_cv_have_gmtoff="yes"], 
   [cf_cv_have_gmtoff="no"])
])
if test $cf_cv_have_gmtoff = "yes" ; then
  AC_DEFINE(HAVE_GMTOFF)
fi

dnl Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_SETVBUF_REVERSED
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([alarm dup2 endpwent fchdir ftruncate getcwd gethostbyaddr gethostbyname getpagesize inet_ntoa memchr memmove memset select strcasecmp strchr strcspn strdup strncasecmp strpbrk strrchr strspn utime gethostname gettext mkdir mktime rmdir socket strerror strstr strtol strtoul mkstemp setgid setuid snprintf vsnprintf getaddrinfo])

# Whenever both -lsocket and -lnsl are needed, it seems to be always the
# case that gethostbyname requires -lnsl.  So, check -lnsl first, for it
# to be in LIBS before the setsockopt checks are performed.  *However*,
# on SINIX-N 5.43, this is false, and gethostent seems to be a better
# candidate. So, let's use it below instead of gethostbyname, and see.
# Stolen from GNU tar 1.12

AC_CHECK_FUNC(gethostent)
if test $ac_cv_func_gethostent = no; then
  AC_CHECK_LIB(nsl, gethostent)
fi

AC_CHECK_FUNC(setsockopt)
if test $ac_cv_func_setsockopt = no; then
  AC_CHECK_LIB(socket, setsockopt)
fi

AC_REPLACE_FUNCS(mergesort getline inet_ntop)

dnl Check whether the system has IPv6
AC_CACHE_CHECK(for IPv6, cf_cv_ipv6, AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
],[struct sockaddr_in6 *a;
   int b = AF_INET6;
   void *c = &(a->sin6_addr);
   return 0;],[cf_cv_ipv6=yes],[cf_cv_ipv6=no])
)
if test "$cf_cv_ipv6" = "yes"
then
  AC_MSG_NOTICE([------------------------------------------------------])
  AC_MSG_NOTICE([Enabling IPv6.])
  AC_DEFINE(HAVE_IPV6)
else
  AC_MSG_NOTICE([------------------------------------------------------])
  AC_MSG_NOTICE([Disabling IPv6, sticking with IPv4.])
fi
AC_MSG_NOTICE([------------------------------------------------------])
AC_MSG_NOTICE([If this was misdetected, then run either])
AC_MSG_NOTICE([  env cf_cv_ipv6=no /bin/sh ./configure YOUR_OPTIONS])
AC_MSG_NOTICE([or])
AC_MSG_NOTICE([  env cf_cv_ipv6=yes /bin/sh ./configure YOUR_OPTIONS])
AC_MSG_NOTICE([(of course, you need to replace YOUR_OPTIONS)])
AC_MSG_NOTICE([------------------------------------------------------])

dnl The 'pcre' library uses non-standard defines
test ".$ac_cv_func_strerror" != ".yes" && \
test ".$cf_cv_have_sys_errlist" = ".yes" && \
	PCRE_DEFINES="$PCRE_DEFINES -DSTRERROR_FROM_ERRLIST"
test ".$ac_cv_func_memmove" != ".yes" && \
test ".$cf_cv_good_bcopy" = ".yes" && \
	PCRE_DEFINES="$PCRE_DEFINES -DUSE_BCOPY"

AC_SUBST(PCRE_DEFINES)

echo
echo "=== configuration summary: ("NONE" is ok here and means the default) ==="
printf "prefix:     " ; eval echo "$prefix"
printf "sysconfdir: " ; eval echo "$sysconfdir"
printf "spooldir:   " ; eval echo "$SPOOLDIR"
printf "lockfile:   " ; eval echo "$LOCKFILE"
echo

AC_OUTPUT(Makefile leafnode.spec leafnode.8 newsq.1 applyfilter.8
fetchnews.5 fetchnews.8 rnews.8 texpire.8 checkgroups.8)
