dnl autoconf script for leafnode -*- m4 -*-
dnl written by Cornelius Krasel & Matthias Andree, 1999 - 2001
dnl written by Matthias Andree, 2002 - 2006
dnl Process this file with autoconf to produce a configure script.
AC_INIT
AC_CONFIG_SRCDIR([leafnode.h])
AM_INIT_AUTOMAKE([leafnode],[2.0.0.alpha20070108a])
AC_PREREQ(2.59)
AC_CONFIG_SRCDIR(leafnode.h)
AC_CONFIG_HEADERS([config.h])
dnl AM_MAINTAINER_MODE

dnl Checks for programs.
AC_C_INLINE
AC_C_VOLATILE
AC_CHECK_TOOL([STRIP],[strip])
AC_PATH_PROG([ID], [id], [false], /usr/xpg4/bin:/usr/bin:/bin)
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_CHECK_TOOL(AR,ar,ar)
AC_PATH_PROG(LYNX,lynx,no)
AC_PATH_PROG(RPM,rpm)
AC_PATH_PROG(GZIP,gzip,false)
AC_PATH_PROG(BZIP2,bzip2,false)
AC_PATH_PROG(SED,sed,false)
AC_PATH_PROG(DEFAULTMTA,sendmail,/usr/sbin/sendmail,/usr/sbin:/usr/lib:/usr/etc)

if test "$GCC" = yes ; then
    AC_MSG_CHECKING([if $CC is really Intel C++])
    case "`$CC -V 2>&1`" in
	[[iI][nN][tT][eE][lL]]*)	ICC=yes ;;
	*)			ICC=no ;;
    esac
    AC_MSG_RESULT($ICC)
fi

dnl If we're using gcc, enable some warnings
AC_CACHE_CHECK(for additional compiler options, ac_cv_prog_gcc_flags, [
ac_cv_prog_gcc_flags=""
if test "x$GCC" = xyes && test "$ICC" = no
then
  echo "void dummy(void);" >configure-dummy.c
  echo "void dummy(void) {}" >>configure-dummy.c
  for i in -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Waggregate-return -Wmissing-declarations -Wmissing-format-attribute -Wnested-externs -Wformat=2 -ggdb -fno-common -Wsequence-point -Wswitch -Wunused -Wuninitialized -Wundef ; do
    $CC $i $ac_cv_prog_gcc_flags -c configure-dummy.c >/dev/null 2>&1 && 
    ac_cv_prog_gcc_flags="$ac_cv_prog_gcc_flags $i"
  done
fi])
rm -f configure-dummy.c configure-dummy.o
CFLAGS="$CFLAGS $ac_cv_prog_gcc_flags"
export CFLAGS
AC_SUBST(CFLAGS)

dnl save what we've found out so far
AC_CACHE_SAVE

dnl check which user to use
AC_MSG_CHECKING(runas-user)
AC_ARG_ENABLE(runas-user, AS_HELP_STRING(--enable-runas-user=USER,user (news)),
  :,
  enableval=news
)
RUNAS_USER=$enableval
AC_MSG_RESULT($RUNAS_USER)
AC_MSG_CHECKING(runas-user uid)
if test x$RUNAS_USER = xroot -o x$RUNAS_USER = xtoor ; then
    AC_MSG_ERROR([user $RUNAS_USER cannot be used to run leafnode for security reasons.])
fi
a=`$ID -u $RUNAS_USER`
if test -z "$a" ; then
    AC_MSG_ERROR([user $RUNAS_USER does not exist. Please add this user before building leafnode.
                  Most systems have man adduser or man useradd to tell you how to do this.])
else
    AC_MSG_RESULT($a)
    if test "$a" -eq 0 ; then
	AC_MSG_ERROR([user $RUNAS_USER cannot be used to run leafnode for security reasons.])
    fi
fi
AC_SUBST(RUNAS_USER)

dnl check which group that user belongs to
AC_MSG_CHECKING(the primary group of $RUNAS_USER)
RUNAS_GROUP=`$ID -g $RUNAS_USER`
AC_MSG_RESULT($RUNAS_GROUP)
if test -z "$a" ; then
    AC_MSG_WARN([cannot determine primary group of ${RUNAS_USER}.])
else
    if test "$a" -eq 0 ; then
	AC_MSG_ERROR([user $RUNAS_USER is in the root or wheel group and cannot be used to run leafnode for security reasons.])
    fi
fi
AC_SUBST(RUNAS_GROUP)

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STAT
AC_HEADER_STDBOOL
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS([crypt.h errno.h fcntl.h limits.h sys/time.h syslog.h string.h strings.h unistd.h arpa/inet.h netdb.h netinet/in.h stddef.h sys/param.h sys/socket.h utime.h stdint.h inttypes.h security/pam_appl.h grp.h])

dnl Check whether all necessary include files are there
AC_CACHE_CHECK(for system include files, cf_cv_sysheaders, AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/param.h>]], [[
     return 0;]])],[cf_cv_sysheaders="yes"],[cf_cv_sysheaders="no"])
)
if test "x$cf_cv_sysheaders" = "xno"
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
AC_CACHE_CHECK(for socklen_t,cf_cv_socklen_t, AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>]], [[
    socklen_t fodder;
    return fodder;]])],[cf_cv_socklen_t="yes"],[cf_cv_socklen_t="no"])
)
if test "x$cf_cv_socklen_t" = "xyes"
then
  AC_DEFINE(HAVE_SOCKLEN_T, 1,
	    [Define if your system defines a socklen_t type.])
fi

dnl check whether to use dmalloc
AM_WITH_DMALLOC

dnl check where to put the spooldir
if test "x$localstatedir" = 'x${prefix}/var' \
  -a "(" "x$prefix" = xNONE -o "x$prefix" = "x/usr" ")" ; then
  SPDIR='/var/spool/news'
else
  SPDIR="$localstatedir/spool/news"
fi

AC_MSG_CHECKING(spooldir)
AC_ARG_ENABLE(spooldir, AS_HELP_STRING(--enable-spooldir=DIR,news spool directory (/var/spool/news)),
  if echo "$enableval" | egrep >/dev/null -v '^/' ; then
    AC_MSG_ERROR(you must give an absolute path)
  fi
  enableval=`echo "$enableval" | sed 'sx/*$xx;'`
,
  enableval=$SPDIR
)
SPOOLDIR=$enableval
AC_MSG_RESULT($SPOOLDIR)
AC_SUBST(SPOOLDIR)

AC_MSG_CHECKING(sysconfdir)
dnl check where to put the confdir
if test "x$sysconfdir" = 'x${prefix}/etc' \
  -a "(" "x$prefix" = xNONE -o "x$prefix" = "x/usr" ")" ; then
  CONFDIR='/etc/leafnode'
else
    case "$sysconfdir" in
	*/) CONFDIR="${sysconfdir%%/}" ;;
	*)  CONFDIR="${sysconfdir}" ;;
    esac
fi
AC_MSG_RESULT($CONFDIR)
if test "x$CONFDIR" != "x$sysconfdir"; then
  AC_MSG_WARN([overriding sysconfdir to $CONFDIR (old: $sysconfdir)])
  AC_MSG_WARN([use --sysconfdir to fix])
  AC_MSG_WARN([use --sysconfdir='\${prefix}/etc' to get])
  AC_MSG_WARN([the default autoconf behaviour])
fi
sysconfdir="$CONFDIR"
AC_SUBST(sysconfdir)

dnl check for rpm build dir
AC_MSG_CHECKING(rpm build dir)
d=/usr/src/packages
test -d ${d}/SPECS || d=/usr/src/redhat dnl RedHat
test -d ${d}/SPECS || d=none
RPMSRC=${d}
AC_MSG_RESULT(${RPMSRC})
test "x${RMPSRC}" = "xnone" || AC_SUBST(RPMSRC)

dnl Check for libcrypt
AC_CHECK_LIB(crypt, crypt)

AC_CACHE_SAVE

dnl Check for PCRE library.
AC_PATH_PROG(PCRECONFIG,pcre-config,AC_MSG_ERROR(pcre-config not found, make sure you have the pcre and pcre-devel packages installed))

CF=`$PCRECONFIG --cflags`
case $CF in ?*)
AC_MSG_NOTICE([adding $CF to CFLAGS]); CFLAGS="$CFLAGS $CF"; export CFLAGS
;; esac


LF=`$PCRECONFIG --libs`
case $LF in ?*)
AC_MSG_NOTICE([adding $LF to LDFLAGS]); LDFLAGS="$LDFLAGS $LF"; export LDFLAGS
;; esac

AC_CACHE_CHECK(for pcre.h,ac_cv_header_pcre_h,[
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <pcre.h>]], [[return 0;]])],[ac_cv_header_pcre_h=yes],[ac_cv_header_pcre_h=no])
])

if test "x$ac_cv_header_pcre_h" = xyes
then
  AC_CHECK_LIB(pcre, pcre_compile)
fi

if test "x$ac_cv_lib_pcre_pcre_compile" = xyes
then
  AC_DEFINE(HAVE_LIBPCRE)
else
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
    AC_MSG_ERROR(PCRE library not found)
    exit 1
fi

AC_SUBST(PCRELIB)
AC_SUBST(LINKPCRELIB)

dnl Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_UTIME_NULL
AC_FUNC_CHOWN
AC_FUNC_FORK dnl checks whether fork() is just a stub
dnl AC_FUNC_MKTIME
if test "$ICC" != yes
then
AC_FUNC_SETVBUF_REVERSED
fi
AC_FUNC_STRFTIME
dnl AC_FUNC_UTIME_NULL dnl checks whether utime(FILE, NULL) uses current time
AC_FUNC_VPRINTF
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([alarm dup2 endpwent fchdir ftruncate getcwd getpagesize inet_ntoa memchr memmove memset select strcasecmp strchr strcspn strncasecmp strpbrk strrchr strspn utime gethostname gettext mkdir rmdir socket strerror strstr strtol strtoul setgid setuid getaddrinfo gai_strerror gettimeofday putenv setgroups])
dnl THESE ARE DISABLED AND UNNEEDED, no malloc(0) calls possible
dnl AC_FUNC_MALLOC dnl checks whether malloc(0) works
dnl AC_FUNC_REALLOC dnl checks whether realloc(0) works

dnl Check if system vsnprintf works
AC_CACHE_CHECK([if your system's vsnprintf works],
	       ln_cv_working_vsnprintf,
AC_RUN_IFELSE([[
	       `cat $srcdir/xsnprintf.c`
	       ]], ln_cv_working_vsnprintf=yes,
	       ln_cv_working_vsnprintf=no,ln_cv_working_vsnprintf=no))

if test "x$ln_cv_working_vsnprintf" = "xyes" ; then
	       AC_DEFINE(HAVE_VSNPRINTF, 1, [Define to 1 if you have a _working_ `vsnprintf' function.]) dnl `
else
AC_LIBOBJ(vsnprintf)
fi

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

AC_REPLACE_FUNCS(arc4random mergesort mkstemp inet_ntop strcasestr)

dnl Check whether the system has IPv6
AC_ARG_ENABLE(IPv6, AS_HELP_STRING(--disable-IPv6,disable IPv6 support (autodetect)),
    IPV6=$enableval ,
    IPV6=auto)

AC_MSG_CHECKING(IPv6 support)
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
]], [[struct sockaddr_in6 *a;
   int b = AF_INET6;
   void *c = &(a->sin6_addr);
   char *e = gai_strerror(0);
   return 0;]])],
   [ if test "x$IPV6" != "xno" ; then cf_cv_ipv6=yes ; else cf_cv_ipv6=no ; fi],
   [cf_cv_ipv6=no])
AC_MSG_RESULT($cf_cv_ipv6)
if test "x$cf_cv_ipv6" = "xyes"
then
  AC_DEFINE(HAVE_IPV6, 1, [If set, enable IPv6.])
fi

dnl The 'pcre' library uses non-standard defines
test ".$ac_cv_func_strerror" != ".yes" && \
test ".$cf_cv_have_sys_errlist" = ".yes" && \
	PCRE_DEFINES="$PCRE_DEFINES -DSTRERROR_FROM_ERRLIST"
test ".$ac_cv_func_memmove" != ".yes" && \
test ".$cf_cv_good_bcopy" = ".yes" && \
	PCRE_DEFINES="$PCRE_DEFINES -DUSE_BCOPY"

AC_SUBST(PCRE_DEFINES)

# Check for PAM
PAM_MSG="no"
AC_ARG_WITH(pam,
	    AS_HELP_STRING([--with-pam],[build with PAM support [[no]]]),
        [
                if test "x$withval" != "xno" ; then
                        if test "x$ac_cv_header_security_pam_appl_h" != "xyes" ; then
                                AC_MSG_ERROR([PAM headers not found])
                        fi

                        AC_CHECK_LIB(dl, dlopen, , )
                        AC_CHECK_LIB(pam, pam_authenticate, , AC_MSG_ERROR([*** libpam missing]))

                        PAM_MSG="yes"

                        AC_DEFINE(USE_PAM, [], [Use pam])
                 fi
        ]
)

if test x$silent != xyes ; then
    echo
    echo "=== configuration summary: ("NONE" is ok here and means the default) ==="
    printf "prefix:      " ; eval echo "$prefix"
    printf "sysconfdir:  " ; eval echo "$sysconfdir"
    printf "spooldir:    " ; eval echo "$SPOOLDIR"
    printf "RUNAS_USER:  " ; eval echo "$RUNAS_USER"
    printf "RUNAS_GROUP: " ; eval echo "$RUNAS_GROUP"
    printf "With PAM:    " ;      echo "$PAM_MSG"
    echo
fi

AC_CONFIG_FILES([Makefile tools/sendbatch.bash leafnode.spec leafnode.8 newsq.1
applyfilter.8 filterfile.5 fetchnews.8 moderators.5 rnews.8 texpire.8 checkgroups.8
sendbatch.1 leafnode-version.1])
AC_OUTPUT
