/** \file store.c
 * Store article into news store.
 * Copyright 2001 by Matthias Andree <matthias.andree@gmx.de>
 * \copyright 2001
 * \author Matthias Andree
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

/*
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

   ADVANTAGES:
   - just one store function
   - can be streamed into
   - message-id/newsgroups need not be known in advance
*/

/*@observer@*/ const char *
store_err(int i)
{
    switch (i) {
    case 0:
	return "success";
    case -1:
	return "OS trouble: %m";
    case -2:
	return "duplicate";
    case -3:
	return "malformatted";
    case 1:
	return "killed by filter";
    default:
	return "unknown";
    }
}

#define BAIL(r,msg) do { rc = (r); if (*msg) ln_log(LNLOG_SERR, LNLOG_CARTICLE, ("store: " msg)); goto bail; } while(0);

/** Read an article from input stream and store it into message.id and
 *  link it into the newsgroups.
 * \return
 * -  0 for success
 * - -1 for OS error
 * - -2 if duplicate
 * - -3 if required header missing or duplicate
 * - -4 if short read or data leftover (with maxbytes)
 * -  1 if article killed by filter
 */
int
store_stream(FILE * in /** input file */ ,
	     int nntpmode /** if 1, unescape . and use . as end marker */ ,
	     const struct filterlist *f /** filters or NULL */ ,
	     ssize_t maxbytes /** maximum byte count, -1 == unlimited */,
	     int delayflg /** delayed download, 0: no 1: pseudo head, 2: all */)
{
    int rc = -1;		/* first, assume something went wrong */
    mastr *tmpfn = mastr_new(4095l);
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
	(void)log_unlink(mastr_str(tmpfn));
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
	switch (toupper(line[0])) {
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
		supersede_cancel(line + 11, "Supersede", "Superseded");
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
		    supersede_cancel(p + 7, "Cancel", "Cancelled");
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

    /* check if we already have the article, ignore if downloading delayed body */
    if (delayflg != 2 && ihave(mid)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "store: duplicate article %s", mid);
	rc = -2;
	goto bail;
    }

    /* check if it is to be filtered, if so, filter */
    if (f && killfilter(f, mastr_str(head))) {
	ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
	       "store: article %s rejected by filter", mid);
	rc = 1;
	goto bail;
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
	nglist = critmalloc(nglistlen * sizeof(char *), "store");
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
	    && isspace((unsigned char)*(q - 1)))
	    continue;		/* skip if duplicate */
	if (create_all_links 
            || is_interesting(name) 
            || is_alllocal(name) 
            || is_dormant(name)) 
        {
	    g = findgroup(name, active, -1);
	    if (g) {
		int ls = 0;
		(void)chdirgroup(name, TRUE);
		if (touch_truncate(LASTPOSTING))
		    BAIL(-1, "cannot touch " LASTPOSTING);

		/* advance pointer when it was a pseudo article */
		if (g->last < g->first)
		    g->last = g->first;

		for (;;) {
		    str_ulong(nb, ++g->last);
		    /* we use sync_link on the always-open file below */
		    /* ls = !sync_link(tmpfn, nb); */
		    ls = !link(mastr_str(tmpfn), nb);
		    if (ls)
			/*@innerbreak@*/ break;
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
	supersede_cancel(mid, "Delaybody body-fetch", "Body fetched");
    }

    /* now create link in message.id */
    m = lookup(mid);
    if (link(mastr_str(tmpfn), m)) {
	if (errno == ENOENT) {	/* message.id file missing, create */
	    if (0 == mkdir_parent(m, MKDIR_MODE))
		if (0 == link(mastr_str(tmpfn), m))
		    goto cont;
	}
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "store: cannot link %s to %s: %m", mastr_str(tmpfn), m);
	rc = -1;
	goto bail;
    }
  cont:
    if (log_fsync(fileno(tmpstream))) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE, "store: cannot fsync: %m");
	(void)log_unlink(m);
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
    (void)log_unlink(mastr_str(tmpfn));
    mastr_delete(tmpfn);
    return rc;
}

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
