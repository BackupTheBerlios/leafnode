To our all-time favourite emacs: this is -*-outline-*- mode.

* History of 2.0

Note leafnode 2.0 is not yet released. These are pre-beta tests.

** History of 2.0b8_ma1 -- changes since past:

- merge official b5->b6, b6->b7 and b7->b8 in (resurrects delposted)
  NOTE: MAY BREAK SEVERAL THINGS, PLEASE REPORT

- start new ChangeLog to mention all changes against baseline version

** History of 2.0b5_ma8 -- changes since 2.0b5_ma7:

- install newsq setgid news to be able to protect leafnode's spooldir.

- remove some debug=0 and debug=debugmode-occurrences. suppressing
  information is no good for debugging.

- junk delposted entirely. No traces left. 

- when posting, try STAT before HEAD (less overhead in case the article
  is already there)
  
- forward port of "files left open" bug fix from 1.9.18 (reported by
  Carl D. Cravens, fixed by Cornelius Krasel)

- NEWNEWS command uses .overview for speed (good for high-traffic
  groups)

- new internal function cuttab, which breaks a field out of a
  tab-separated string and returns a copy (VERY useful for .overview
  handling). The string returned must be free(3)d.

- fix broken -P option (checkforpostings expected all-numeric file
  names, while leafnode's spool files also have two "-" characters in them)
  (Reported by Raymond Scholz) 

- groupexpire fixed, NOTE: NEW SYNTAX groupexpire=de.rec.fotografie 10
  (Reported by Raymond Scholz) Also note that the old 2.0b5 parser could
  crash on bogus data in config file here.

** History of 2.0b5_ma7 -- changes since 2.0b5_ma6:

- portability workaround for broken Solaris /bin/sh: 
  replace test -e by test -f; note that SUSv2 requires test -e
  (reported by Johannes Teveﬂen)

- link own libraries before system libraries

- fix Makefile to heed CFLAGS, CPPFLAGS and LDFLAGS properly (as per
  autoconf and make documentation)

- merge libugid.a into liblnutil.a

- Makefile cleanups

- fix installation procedure, no longer takes over existing directories
  except its spool directory (reported by Johannes Teveﬂen)

- fix broken make clean if pcre missing (reported by Johannes Teveﬂen)

- fix link count type in artutil.c:281, explicit cast to long.
 
- ln_log now has ln_log_so which logs to syslog and stdout

- nntpd now redirects stderr to /dev/null to prevent initialization
  errors from being sent to the client

- nntpd now logs unknown options at WARNING level (was NOTICE)

- fix broken @VERSION@ in texpire.8.in

** History of 2.0b5_ma6 -- changes since 2.0b5_ma5:

NOTE that the new locking scheme is not in place yet.

- provide replacement for sprintf(buf,"%lu",something): str_ulong

- provide replacement for inet_ntop (uses inet_ntoa)
  note that compiling --with-ipv6 will fail in that case.

- Makefile fix: don't append {0,0} in configparam_data.c, should fix
  configparam related crashes (reported by Stefan Fleiter)

- remove spurious free(c) in activutil.c:215 which caused crashes after
  activutil was done. (reported by Michael Mauch)

- change if(optind+1<argc) to >argc in applyfilter.c:75 which caused
  immediate segfault when called without newsgroup (reported by
  Michael Mauch)

  NOTE that applyfilter currently only deals with one newsgroup per invocation!

- fix all numeric constants around fqdn to FQDN_SIZE

- fix some explicit ==/!= 0/NULL checks

- merge "xover error: Message ID does not end with >" in one if branch

- fix some printf to ln_log

- log unknown configuration parameters

** History of 2.0b5_ma5 -- changes since 2.0b5_ma4:

NOTE that the new locking scheme is not in place yet.

- squash all whitespace on the inner side of parentheses.

- remove gperf dependency, not portable enough. as replacement, now
  use qsort and bsearch. New file config.table, requires awk at build
  time to generate configparam_data.c and config_defs.h. New configparam.c.

- make dist builds bzip2 without PCRE

- various logging consistency fixes, remove excess LF in some ln_log files.

- remove some if(verbose > XX) in favor of ln_log.

- minor Makefile fixes to reduce rebuilding of components already built

- new b_sortnl.c (build-time utility to overcome sort LC_COLLATE
  madness, AT&T's Graphviz was trapped by that one)

- new h_error.h and .c to give symbolic output of gethostbyname h_errno.

- improved error checking in nntpconnect (check for dup and
  gethostbyname failures)

** History of 2.0b5_ma4 -- changes since 2.0b5_ma3:

- Fixes to IPv6 in nntpd.c -- PLEASE CHECK

- Further fixes to console logging (leftover %m formats)

- Fix local.groups handling

** History of 2.0b5_ma3 -- changes since 2.0b5_ma2:

- Now has a new configuration file switch named avoidxover, if that is
  non-zero, then prefer XHDR over XOVER (only possible if not delaying
  bodies and not filtering).

- Reintroduce XOVER, make preferring XOVER over XHDR the default. INN is
  DEAD slow on XHDR, and INN is in widespread use.

- No longer uses syslog directly (avoids excess printf statements), but
  has a all-in-one-log-to-syslog-and-console function. system
  dependencies encapsulated in separate file.

- miscutil.c: bugfix, drops root privileges BEFORE making
  directories. Now makes all needed directories. (Removed from Makefile).

- Bugfixes with unsigned long wrapping in fetchnews.c, which would then
  fetch the world on the second invokation. Also recognizes "no new
  news" again. Fixes to off-by-one errors.

- SECURITY bugfixes in rnews.c. (was vulnerable against all kinds of
  /tmp races, now uses mkstemp).

- Add some missing strerror(errno) to logging.

Maintainer only: Now uses gperf to speed up parsing, currently only for 
config file.

** History of 2.0b5_ma2 -- changes since 2.0b5-ma1:

NOTE: This version changes the dash in -ma1 to an underscore in _ma2 for
----- RPM compatibility.

Compiling on FreeBSD 4.0, I found some problems in syslog calls. Not
sure if they are security relevant, you'd better update.

Makefile.in:
============
20001112: Add support for separate critmem and getline/getaline
	  compilation. Remove unnecessary object file rules, replacing
	  them by a .c.o: rule. Fix missing rnews dependency in install.
	  Work around broken directory handling in dist. Move VERSION to
	  configure.in to allow replacement in leafnode.spec.in. Take
	  RPMSRC from configure. Introduce HDRFILES for proper
	  dist. Change rpm target.
