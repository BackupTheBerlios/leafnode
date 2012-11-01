dnl autoconf script for leafnode -*- m4 -*-
dnl written by Cornelius Krasel & Matthias Andree, 1999 - 2001
dnl written by Matthias Andree, 2002 - 2012
dnl Process this file with autoconf to produce a configure script.
AC_INIT
AC_CONFIG_SRCDIR([leafnode.h])
AM_INIT_AUTOMAKE([leafnode],[2.0.0.alpha20121101a],[silent-rules])
AM_SILENT_RULES([yes])
AC_PREREQ([2.68])
AC_CONFIG_SRCDIR(leafnode.h)
AC_CONFIG_HEADERS([config.h])
dnl AM_MAINTAINER_MODE

dnl Checks for programs.
AC_C_INLINE

AC_CHECK_TOOL([STRIP],[strip])
AC_PATH_PROG([ID], [id], [false], /usr/xpg4/bin:/usr/bin:/bin)
AC_PROG_AWK
AC_PROG_CC
AC_PROG_CC_C99
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
  for i in -Wall -W -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wbad-function-cast -Wcast-qual -Wcast-align -Wwrite-strings -Waggregate-return -Wmissing-declarations -Wmissing-format-attribute -Wnested-externs -Wformat=2 -ggdb -fno-common -Wsequence-point -Wswitch -Wunused -Wuninitialized -Wundef -Wc++-compat; do
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
AC_CHECK_HEADERS([crypt.h sys/param.h security/pam_appl.h])

dnl Check whether all necessary include files are there
AC_CACHE_CHECK(for system include files, cf_cv_sysheaders, AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/param.h>]], [[
     return 0;]])],[cf_cv_sysheaders="yes"],[cf_cv_sysheaders="no"])
)
if test "x$cf_cv_sysheaders" = "xno"
then
  AC_MSG_ERROR(Kernel sources such as sys/types.h not installed - unable to continue)
fi

dnl check whether to use dmalloc
AM_WITH_DMALLOC

dnl check where to put the spooldir
if test "x$localstatedir" = 'x${prefix}/var' \
  -a "(" "x$prefix" = xNONE -o "x$prefix" = "x/usr" ")" ; then
  SPDIR='/var/spool/leafnode'
else
  SPDIR="$localstatedir/spool/leafnode"
fi

AC_MSG_CHECKING(spooldir)
AC_ARG_ENABLE(spooldir, AS_HELP_STRING(--enable-spooldir=DIR,news spool directory (/var/spool/leafnode)),
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
test -d ${d}/SPECS || d=/usr/src/redhat
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
AC_CHECK_FUNCS([setgroups])

# Whenever both -lsocket and -lnsl are needed, it seems to be always the
# case that gethostbyname requires -lnsl.  So, check -lnsl first, for it
# to be in LIBS before the setsockopt checks are performed.  *However*,
# on SINIX-N 5.43, this is false, and gethostent seems to be a better
# candidate. So, let's use it below instead of gethostbyname, and see.
# Stolen from GNU tar 1.12

AC_CHECK_LIB(nsl, gethostent)
AC_CHECK_LIB(socket, setsockopt)
AC_REPLACE_FUNCS(arc4random mergesort strcasestr strlcpy)

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
