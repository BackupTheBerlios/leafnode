/** \file lockfile.c 
 *  library module to safely create a lock file.
 *  \author Matthias Andree
 *  \date 2001 - 2002
 *
 *  Copyright (C) 2001 - 2002  Matthias Andree <matthias.andree@web.de>
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA 
 */

#include "leafnode.h"
#include "ln_log.h"
#include "critmem.h"
#include "validatefqdn.h"
#include "mastring.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <signal.h>		/* for kill */

/** get hard link count of open file pointed to by filedes.  
 * uses fstat(2)
 * \return -1 in case of trouble
 * (which is also logged), the count of hard links otherwise
 */
static nlink_t
fd_st_nlink(const int fd /** open file descriptor */ )
{
    struct stat st;

    if (fstat(fd, &st)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Cannot fstat %d: %m", fd);
	return -1;
    }

    return st.st_nlink;
}

/** Check if the lock file given by "name" is stale and if so, erase it.
 * A lock is stale when the process to which it belongs, is dead.
 *
 * \bug cannot detect if lock files held by other hosts are stale.
 *
 * \return 
 *         - -1 for error "failure"
 *         - 0 if lock file is still in use or held by another host "failure"
 *         - 1 if lock file is stale and has been erased "success" */
static int
lock_is_stale(
/** file name of lock file */
		 const char *const name,
/** quiet flag */
		 const int quiet)
{
    char buf[512];
    int fd;
    int len;
    char *pid;
    char *host;
    char *tmp;
    unsigned long npid;

    fd = open(name, O_RDONLY, 0);
    if (fd < 0) {
	if (errno == ENOENT) {
	    /* file has just disappeared, thus it's not stale */
	    return 0;
	} else {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "cannot open %s for reading: %m", name);
	    return -1;
	}
    }

    if ((len = read(fd, buf, sizeof(buf) - 1)) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "read error on %s: %m", name);
	(void)close(fd);
	return -1;
    }

    if (close(fd) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "read error on %s: %m", name);
	return -1;
    }

    /* read pid and host */
    buf[len - 1] = '\0';
    pid = host = buf;
    /* we expect a single \n here */
    host += strcspn(host, "\n");
    *(host++) = '\0';

    /* kill trailing \n */
    tmp = host;
    tmp += strcspn(tmp, "\n");
    *tmp = '\0';

    npid = strtoul(pid, 0, 10);
    if (npid == ULONG_MAX && errno == ERANGE) {
	/* overflow error, should not happen, bail out */
	ln_log(LNLOG_SERR, LNLOG_CTOP, "bogus pid in %s: %m", name);
	return -1;
    }

    if (strcasecmp(host, fqdn)) {
	if (!quiet)
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "lockfile held by pid %lu on host %s, we're %s",
		   npid, host, fqdn);
	return 0;		/* other host holds the lock */
    }

    /* okay, we can see if there's still a process with that pid active */
    if (kill((pid_t)npid, 0) && errno == ESRCH) {
	/* no such process, good */
	if (!unlink(name)) {
	    ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
		   "erased stale pid %lu host %s lockfile %s",
		   npid, host, name);
	    return 1;
	} else {
	    if (!quiet)
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "unable to erase stale pid %lu host %s lockfile %s",
		       npid, host, name);
	    return 0;
	}
    }

    /* there is a process active */
    return 0;
}

/** Safe mkstemp replacement.  
 * Ensures the file is only read- and writable by its owner; some systems
 * create these with 0777 or 0666 permissions. */
int
safe_mkstemp(
/** template to build filename upon, as for mkstemp */
		char *templ)
{
    mode_t oldmask;
    int ret;

    oldmask = umask(077);
    ret = mkstemp(templ);
    (void)umask(oldmask);

    return ret;
}


/**
 * Try to set a lockfile, blocking or non-blocking.
 * Checks if the lockfile exists and is active.
 *
 * requires: atomic link(2) syscall.
 *
 * features: 
 *  - NFS safe (but leafnode does not work distributed)
 *  - stale detection by PID in lock file (may be fooled)
 *
 * \bug Cannot remove stale lock on other machine.
 *
 * \bug Stale detection may be fooled if another process has been
 * assigned the PID that the last caller had.
 *
 * \return
 * - 0: if locking succeeded
 * - 1: if locking failed because the lock is held by someone else and
 *     isn't stale
 * - -1: for other errors
 */
int
attempt_lock(
/** Timeout, if nonzero, wait at most this many seconds. */
		   unsigned long timeout)
{
    const int block = 1;
    char *l2, *pid, *p;
    int fd;
    int have_lock = 0;
    int quiet = 0;
    const char *const append = ".XXXXXXXXXX";
    const int have_timeout = (timeout != 0);

    if (debugmode)
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	       "lockfile_exists(timeout=%lu), fqdn=\"%s\"",
	       timeout, fqdn);

    /* kill bogus fqdn */
    if (!is_validfqdn(fqdn)) {
	ln_log(LNLOG_SCRIT, LNLOG_CTOP,
	       "Internal error: "
	       "must not try to lock with local host name \"%s\"", fqdn);
	return -1;
    }

    l2 = (char *)critmalloc(strlen(lockfile) + strlen(append) + 1,
			    "lockfile_exists");
    pid = (char *)critmalloc(strlen(fqdn) + sizeof(unsigned long) * 4 + 4,
			     "lockfile_exists");

    p = mastrcpy(l2, lockfile);
    (void)mastrcpy(p, append);

    /* make a temporary file */
    fd = safe_mkstemp(l2);
    if (fd < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "mkstemp(%s) failed: %m", l2);
	free(l2);
	free(pid);
	return -1;
    }

    /* write our PID and host into it (stale detection) */
    sprintf(pid, "%lu\n%s\n", (unsigned long)getpid(), fqdn);
    /* safe, see malloc above */
    if (writes(fd, pid) < 0 || fsync(fd) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot write to %s: %m", l2);
	if (unlink(l2))
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "Cannot remove lock helper file %s: %m", l2);
	free(l2);
	free(pid);
	return -1;
    }

    /* and try to finally lock */
    while (!have_lock) {
	if (0 == link(l2, lockfile)) {
	    /* link succeeded. good. */
	    have_lock = 1;
	    break;
	} else {
	    if (2 == fd_st_nlink(fd)) {
		/* link failed, but st_nlink has increased to 2, good. */
		have_lock = 1;
	    } else {
		int stale;
		struct timeval tv = { 1, 0 };

		/* Could not create link. Check if the lock file is stale. */
		stale = lock_is_stale(lockfile, quiet);

		/* if we have a stale file, it's just been removed.
		   retry, don't care for what block says */
		if (stale == 1)
		    continue;

		quiet = 1;

		/* if we have a problem with stale detection, or
		   if we are in non-blocking mode, abort */
		if (stale == -1 || !block)
		    break;

		if (have_timeout) {
		    if (timeout == 0)
			break;

		    --timeout;
		}

		/* retry after a second, select does not interfere w/ alarm */
		if (select(0, NULL, NULL, NULL, &tv) < 0) {
		    /* must not happen */
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "lockfile_exists: select failed: %m");
		    break;
		}
	    }
	}
    }

    if (close(fd) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot write to %s: %m", l2);
	have_lock = 0;
    }

    /* unlink l2, but just log if unable to unlink, ignore otherwise */
    if (unlink(l2))
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Cannot remove lock helper file %s: %m", l2);

    /* clean up */
    free(l2);
    free(pid);

    /* mind the return logic */
    return have_lock ? 0 : 1;
}

/** Tries to hand over lock to the process with the given pid. \return
 * 0 for success, nonzero for failure -- check errno for details in
 * case of error.
 */
int handover_lock(pid_t pid) {
  int fd = open(lockfile, O_RDWR|O_TRUNC, (mode_t)0600);
  char *buf = (char *)critmalloc(strlen(fqdn) + sizeof(unsigned long) * 4 + 4,
				 "handover_lock");
  if (fd < 0) { free(buf); return fd; }
  sprintf(buf, "%lu\n%s\n", (unsigned long)pid, fqdn);
  if (-1 == writes(fd, buf)) goto close_bail;
  if (-1 == fsync(fd)) goto close_bail;
  if (-1 == close(fd)) goto close_bail;
  free(buf);
  return 0;

 close_bail:
  (void)close(fd); 
  free(buf);
  return -1;
}
