/** \file store.c
 * Store article into news store.
 * Copyright 2001 - 2005 by Matthias Andree <matthias.andree@gmx.de>
 * \date 2001 - 2005
 * \author Matthias Andree
 * \bug Can corrupt .overview files when used across NFS.
 *
 * Modified by Volker Apelt <volker_apelt@yahoo.de>.
 * Copyright of the modifications 2002.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "mastring.h"
#include "format.h"
#include "attributes.h"
#include "ln_dir.h"
#include "msgid.h"
#include "link_force.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/**
 
   WARNING:
============= 
   This function relies on O_APPEND for updating the .overview files, which
   WILL NOT WORK on NFS.


   PLAN:
   - 1. open new article with temporary file name
   - 2a. write headers, dropping xref headers
   - 2b. gather message-id and newsgroups on the fly
   - 2c. gather supersedes/cancel headers on the fly, if any, write into journal
     FIXME: we need to keep track of advance cancels and cancel message-ids.
   - 3. if message-id is known, link into final places except message.id
   - 4. write xref
   - 5. make links into newsgroups
   - 6. write body
   - 7. link into message.id
   - 8. generate .overview line and directly append it

   ADVANTAGES:
   - just one store function
   - can be streamed into
   - message-id/newsgroups need not be known in advance
   - no second pass necessary to clean the .overview files up.
*/

/*@observer@*/ const char *
store_err(int i)
{
    switch (i) {
    case 0:
	return "success";
    case -1:
	return "OS trouble, see previous log messages for details";
    case -2:
	return "duplicate";
    case -3:
	return "malformatted";
    case 1:
	return "killed by filter";
    case 2:
	/* FIXME: might be other reasons, too */
	return "no valid newsgroups";
    default:
	return "unknown";
    }
}

#define BAIL(r,msg) { rc = (r); if (*msg) ln_log(LNLOG_SERR, LNLOG_CARTICLE, ("store: " msg)); goto bail; }

/** Read an article from input stream and store it into message.id and
 *  link it into the newsgroups.
 * \return
 * -  0 for success
 * - -1 for OS error
 * - -2 if duplicate
 * - -3 if required header missing or duplicate
 * - -4 if short read or data leftover (with maxbytes)
 * -  1 if article killed by filter
 * -  2 if article dropped by other reason (no valid newsgroups)
 */
int
store_stream(FILE * in /** input file */ ,
	     int nntpmode /** if 1, unescape . and use . as end marker */ ,
	     const struct filterlist *f /** filters or NULL */ ,
	     ssize_t maxbytes /** maximum byte count, -1 == unlimited */,
	     int delayflg /** delayed download,
			    0 == no, full article,
			    1 == yes, pseudo head
			    2 == yes, full article
			    3 == no, but article is from in.coming
			         and we have a message.id link */
	)
{
    int rc = -1;		/* first, assume something went wrong */
    mastr *tmpfn = mastr_new(LN_PATH_MAX);
    const char *line = 0;
    char *mid = 0, *m;
    mastr *ngs = mastr_new(80l);
    int ngs_just_seen = 0;
    int found_body = 0;
    int c_date = 0;
    int c_from = 0;
    int c_subject = 0;
    int c_path = 0;
    int ignore = 1;
    int olddirfd = -1;		/* we save the cwd here */
    char **nglist = 0;		/* we save the newsgroups here */
    int nglistlen = 10;		/* how big did we allocate nglist */
    char **t = 0;
    int tmpfd = -1;
    mastr *xref = 0;
    char nb[40] = "";		/* buffer for numbers */
    FILE *tmpstream = 0;
    mastr *head = mastr_new(4095l);	/* full header for killing */
    ssize_t s;
    mastr *ln = mastr_new(4095l);	/* line buffer */
    mastr *xowrite = mastr_new(4095l);	/* buffer for overview line */
    char *ov = NULL;			/* hold getxoverline result */

    (void)mastr_vcat(tmpfn, spooldir, "/temp.files/store_XXXXXXXXXX", NULL);

    if (nntpmode && maxbytes != -1) {
	ln_log(LNLOG_SCRIT, LNLOG_CTOP, "store: nntpmode and maxbytes are mutually exclusive for now.");
	abort();
    }

    /* check for OOM */
    if (!head) {
	mastr_delete(ln);
	mastr_delete(tmpfn);
	return -1;
    }

    /* make temp. file */
    tmpfd = safe_mkstemp(mastr_modifyable_str(tmpfn));
    if (tmpfd < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "store: error in mkstemp(\"%s\"): %m",
	       mastr_str(tmpfn));
	mastr_delete(tmpfn);
	mastr_delete(head);
	mastr_delete(ln);
	return -1;
    }

    tmpstream = fdopen(tmpfd, "w+");
    if (!tmpstream) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "store: cannot fdopen(%d): %m", tmpfd);
	(void)log_close(tmpfd);
	(void)log_unlink(mastr_str(tmpfn), 0);
	mastr_delete(tmpfn);
	mastr_delete(head);
	mastr_delete(ln);
	return -1;
    }

    /* copy header
     *  - stripping Xref:, and possibly copying Message-ID: and Newsgroups: 
     *  - looking for Control: cancel and Supersedes:
     *  - check count of each mandatory header
     */
    while ((maxbytes > 0 || maxbytes == -1) &&
	(s = mastr_getln(ln, in, maxbytes)) > 0) {
	if (maxbytes != -1) maxbytes -= s;
	mastr_chop(ln);
	if (debugmode & DEBUG_STORE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		    "store: read %ld bytes: \"%s\", to go: %ld", 
		    (long)mastr_len(ln),
		    mastr_str(ln), (long)maxbytes);
	if (!mastr_len(ln)) {
	    found_body = 1;
	    break;		/* end of headers */
	}
	line = mastr_str(ln);
	if (nntpmode && line[0] == '.') {
	    ++line;
	    if (!*line) {
		/* article has no body */
		ignore = 0;
		BAIL(-3, "article lacks body");
	    }
	}
	if (f) {
	    /* save headers for filter */
	    (void)mastr_cat(head, line);
	    (void)mastr_cat(head, LLS);
	}
	if (ngs_just_seen) {
	    if (line[0] == ' ' || line[0] == '\t') {
		(void)mastr_cat(ngs, line);
	    } else {
		ngs_just_seen = 0;
	    }
	}
	switch (toupper((unsigned char)line[0])) {
	case 'D':
	    if (c_date < 2 && str_isprefix(line, "Date:"))
		++c_date;
	    break;
	case 'F':
	    if (c_from < 2 && str_isprefix(line, "From:"))
		++c_from;
	    break;
	case 'S':
	    if (c_subject < 2 && str_isprefix(line, "Subject:"))
		++c_subject;
	    else if (str_isprefix(line, "Supersedes:")) {
		delete_article(line + 11, "Supersede", "Superseded", 0);
	    }
	    break;
	case 'P':
	    if (c_path < 2 && str_isprefix(line, "Path:"))
		++c_path;
	    break;
	case 'X':
	    if (str_isprefix(line, "Xref:"))
		continue;
	    break;
	case 'M':
	    if (str_isprefix(line, "Message-ID:")) {
		const char *p = line + 11;
		if (mid)
		    BAIL(-3, "more than one Message-ID header found");
		SKIPLWS(p);
		mid = critstrdup(p, "store");
		D(d_stop_mid(mid));
	    }
	    break;
	case 'N':
	    if (str_isprefix(line, "Newsgroups:")) {
		const char *p = line + 11;
		if (mastr_len(ngs) > 0) {
		    BAIL(-3, "more than one Newsgroups header found");
		}
		SKIPLWS(p);
		(void)mastr_cat(ngs, p);
		ngs_just_seen = 1;
	    }
	    break;
	case 'C':
	    if (str_isprefix(line, "Control:")) {
		const char *p = line + 8;
		SKIPLWS(p);
		if (str_isprefix(p, "cancel") && isspace((unsigned char)p[6]))
		    delete_article(p + 7, "Cancel", "Cancelled", 0);
	    }
	    break;
	default:
	    ;
	}

	if (fputs(line, tmpstream) == EOF)
	    BAIL(-1, "write error");
	if (fputs(LLS, tmpstream) == EOF)
	    BAIL(-1, "write error");
    }

    /* check if mandatory headers present exactly once */
    if (c_from != 1)
	BAIL(-3, "More or less than one From header found");
    if (c_date != 1)
	BAIL(-3, "More or less than one Date header found");
    if (c_subject != 1)
	BAIL(-3, "More or less than one Subject header found");
    if (c_path != 1)
	BAIL(-3, "More or less than one Path header found");
    if (NULL == mid)
	BAIL(-3, "No Message-ID header found");
    if (mastr_len(ngs) == 0)
	BAIL(-3, "No Newsgroups header found");

    if (maxbytes == 0 && !found_body)
	BAIL(-4, "Headers extend beyond allowed read range");

    /* check if we already have the article, ignore if downloading
     * delayed body or feeding from in.coming */
    if (delayflg < 2 && ihave(mid)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "store: duplicate article %s", mid);
	rc = -2;
	goto bail;
    }

    /* check if it is to be filtered, if so, filter */
    if (f) {
	mastr_unfold(head);
	if (debugmode & DEBUG_FILTER) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		    "store: try filters on header \"%s\"", mastr_str(head));
	}
	if (killfilter(f, mastr_str(head))) {
	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
		    "store: article %s rejected by filter", mid);
	    rc = 1;
	    goto bail;
	}
    }

    /* We now have the header, we link this into the appropriate
       newsgroup folders, generating the Xref: header, and writing
       it. */

    /* save cwd */
    olddirfd = open(".", O_RDONLY);
    if (olddirfd < 0)
	BAIL(-1, "cannot open current directory");

    /* parse ngs */
    /*@+loopexec@*/
    for (;;) {
	nglist = (char **)critmalloc(nglistlen * sizeof(char *), "store");
	if (str_nsplit(nglist, mastr_str(ngs), ",", nglistlen) >= 0)
	    break;
	/* retry with doubled size */
	free_strlist(nglist);
	free(nglist);
	nglistlen += nglistlen;
    }
    /*@=loopexec@*/

    xref = mastr_new(1024l);
    /* store */
    for (t = nglist; *t; t++) {
	struct newsgroup *g = 0;
	char *name = *t;
	char *q;
	SKIPLWS(name);
	q = name;
	SKIPWORDNS(q);
	*q = '\0';
	/* name now contains the trimmed newsgroup */
	if ((q = strstr(mastr_str(xref), name))
	    && isspace((unsigned char)*(q - 1))
	    && q[strlen(name)] == ':')
	    continue;		/* skip if duplicate */
	if ((create_all_links
            || is_interesting(name)
            || is_localgroup(name))
            && !is_dormant(name))
        {
	    g = findgroup(name, active, -1);
	    if (g) {
		int ls = 0;
		int local;
		(void)chdirgroup(name, TRUE);
		if (touch_truncate(LASTPOSTING))
		    BAIL(-1, "cannot touch " LASTPOSTING);

		local = is_localgroup(g->name) ? 1 : 0;

		/* keep pointers consistent */
		if (g->first < 1) g->first = 1;
		if (g->last < g->first - local) {
		    /* was empty */
		    g->last = g->first - local;
		    g->first++;
		}

		for (;;) {
		    str_ulong(nb, ++g->last);
		    /* we use sync_link on the always-open file below */
		    /* ls = !sync_link(tmpfn, nb); */
		    ls = !link(mastr_str(tmpfn), nb);
		    if (ls) {
			if (log_chmod(nb, 0660) < 0) {
				ls = -1;
			}
			/*@innerbreak@*/ break;
		    }
		    if (errno == EEXIST) {
			int e;
			ln_log(LNLOG_SWARNING, LNLOG_CARTICLE,
			       "store: %s: stale water marks, trying to fix",
			       name);
			e = getwatermarks(&g->first, &g->last, &g->count);
			if (e != 0) {
			    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				   "store: %s: cannot obtain water marks",
				   name);
			    BAIL(-1, "");
			}
			continue;
		    }
		    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
			   "store: error linking %s into %s for %s: %m",
			   mastr_str(tmpfn), nb, name);
		    /*@innerbreak@*/ break;
		}
		if (!ls) {
		    rc = -1;
		    goto bail;
		}
		(void)mastr_vcat(xref, " ", name, ":", nb, NULL);
	    }
	}
    }
    if (mastr_str(xref)[0] == '\0')
	BAIL(2, "no valid newsgroups");
    if (fputs("Xref: ", tmpstream) == EOF)
	BAIL(-1, "write error");
    if (fputs(fqdn, tmpstream) == EOF)
	BAIL(-1, "write error");
    if (fputs(mastr_str(xref), tmpstream) == EOF)
	BAIL(-1, "write error");
    if (fputs(LLS, tmpstream) == EOF)
	BAIL(-1, "write error");

    /* write separator between header and body */
    if (delayflg != 1) {
	if (fputs(LLS, tmpstream) == EOF)
	    BAIL(-1, "write error");
    }

    /* copy body */
    while ((maxbytes > 0 || maxbytes == -1) &&
	(s = mastr_getln(ln, in, maxbytes)) > 0) {
	if (maxbytes != -1) maxbytes -= s;
	mastr_chop(ln);
#if 0
	if (debugmode & DEBUG_STORE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		    "store: read %s", mastr_str(ln));
#endif
	line = mastr_str(ln);
	if (nntpmode && line[0] == '.') {
	    ++line;
	    if (!*line) {
		ignore = 0;
		break;		/* complete */
	    }
	}
	if (fputs(line, tmpstream) == EOF)
	    BAIL(-1, "write error");
	if (fputs(LLS, tmpstream) == EOF)
	    BAIL(-1, "write error");
    }

    if (maxbytes > 0) {
	    ignore = 0;
	    BAIL(-4, "short read while copying body");
    }

    if (fflush(tmpstream))
	BAIL(-1, "write error");

    /* delaybody downloaded: kill old pseudo article header */
    if (delayflg == 2) {
	delete_article(mid, "Delaybody body-fetch", "Body fetched", 0);
    }

    /* now create link in message.id */
    m = lookup(mid);
    if (!m)
	BAIL(-3, "Message-ID header missing or malformatted");
    if (delayflg == 3 ? link_force(mastr_str(tmpfn), m)
	    : link(mastr_str(tmpfn), m)) {
	if (errno == ENOENT) {	/* message.id directory missing, create */
	    if (0 == mkdir_parent(m, MKDIR_MODE))
		if (0 == (delayflg == 3 ? link_force(mastr_str(tmpfn), m) : link(mastr_str(tmpfn), m))) {
		    goto cont;
		}
	}
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "store: cannot link %s to %s: %m", mastr_str(tmpfn), m);
	rc = -1;
	goto bail;
    }
  cont:
    if (log_fsync(fileno(tmpstream))) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE, "store: cannot fsync: %m");
	(void)log_unlink(m, 0);
	rc = -1;
	goto bail;
    }
/*    if (sync_parent(tmpfn) || sync_parent(m)) { rc = -1; goto bail; } */
    if (fclose(tmpstream)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "store: error reported by fclose: %m");
	rc = -1;
	goto bail;
    }

    ov = getxoverline(0, mastr_str(tmpfn));
    if (ov && !strchr(ov, '\t')) {
	free(ov);
	ov = NULL;
    }

    /* iterate over XRef: group:number and update .overview */
    {
	char *q = mastr_modifyable_str(xref);

	SKIPLWS(q);
	while(*q) {
	    char *tt, *p;

	    p = strchr(q, ':');
	    if (!p) break;
	    *p++ = '\0';
	    /* now q has the group name */
	    tt = strchr(p, ' ');
	    if (!tt) tt = p + strlen(p);
	    if (*tt == ' ') *tt++ = '\0';
	    else *tt = '\0';
	    /* now p has the number the article has in the current group */

	    if (ov && chdirgroup(q, FALSE)) {
		int fdo;

		mastr_clear(xowrite);
		mastr_vcat(xowrite, p, strchr(ov, '\t'), "\n", NULL);
		fdo = open(".overview", O_WRONLY|O_APPEND|O_CREAT,
			(mode_t)0660);
		if (fdo >= 0) {
		    /* FIXME: will getxover cope with hosed
		     * overview files? ENOSPC is a candidate... */
		    (void)writes(fdo, mastr_str(xowrite));
		    /* no fsync here, .overview is not precious as it
		     * can be regenerated */
		    (void)close(fdo);
		}
	    }
	    q = tt;
	}
    }

    tmpstream = NULL;
    rc = 0;

bail:
	if (head)
	    mastr_delete(head);
	if (ignore && rc) {
	    if (nntpmode) {
		while ((line = getaline(in)))
		    if (0 == strcmp(line, "."))
			break;
	    } else {
		/* drain bytes */
		if (debugmode & DEBUG_STORE)
		    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
			    "store: ignoring %ld bytes",
			    (long)maxbytes); 
		while (maxbytes > 0 && (s = mastr_getln(ln, in, maxbytes)) > 0) {
		    mastr_chop(ln);
		    maxbytes -= s;
		    if (debugmode & DEBUG_STORE)
			ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
				"store: ignored %ld: %s, "
				"to go: %ld", (long)s, mastr_str(ln), 
				(long)maxbytes); 
		}
	    }
	}
	if (olddirfd >= 0) {
	    (void)fchdir(olddirfd);
	    (void)close(olddirfd);
	}
	if (xref)
	    mastr_delete(xref);
	if (mid)
	    free(mid);
	if (ln)
	    mastr_delete(ln);
	if (nglist) {
	    free_strlist(nglist);
	    free(nglist);
	}
	if (ov) {
	    free(ov);
	}
	mastr_delete(xowrite);
	mastr_delete(ngs);
	if (tmpstream) {
	    (void)fflush(tmpstream);
	    if (rc) {
		/* kill the beast */
		(void)ftruncate(fileno(tmpstream), 0);
		(void)fsync(fileno(tmpstream));
	    }
	    (void)fclose(tmpstream);
	}
	(void)log_unlink(mastr_str(tmpfn), 0);
	mastr_delete(tmpfn);
	return rc;
    }

/** open a file and feed it to store_stream */
int
store(const char *name, int nntpmode,
      /*@null@*/ const struct filterlist *f /** filters or NULL */,
      int delayflg)
{
    FILE *i = fopen(name, "r");
    if (i) {
	int rc = store_stream(i, nntpmode, f, (ssize_t)-1, delayflg);
	(void)fclose(i);
	return rc;
    } else {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		"store: cannot open %s for storing: %m", name);
	return -1;
    }
}
