/** \file artutil.c 
 * stuff dealing with articles.
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1998, 1999.
 * Modifications (C) 2001 - 2002 by Matthias Andree <matthias.andree@gmx.de>
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "mastring.h"
#include "msgid.h"
#include "get.h"

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

/**
 * Return an arbitrary news article header from a memory buffer.
 * Copes with folded header lines. NOTE: The input must be
 * null-terminated.
 * NOTE: calls abort() if header does not contain a colon.
 * \return malloc()ed copy of header without its tag (caller must free
 * that) or NULL
 * \bug should rather use Boyer-Moore or something to be quicker
 */
/*@null@*//*@only@*/ char *
mgetheader(
/** header to find, must contain a colon, must not be NULL */
	      /*@notnull@*/ const char *hdr,
/** buffer to search, may be NULL */
	      /*@null@*/ const char *buf)
{
    mastr *hunt;
    const char *p, *q;
    char *value = NULL;

    /* ensure we have a header to look for */
    assert(hdr != NULL);
    /* if the buffer does not exist, we don't find anything, but we
     * do not barf either. */
    if (NULL == buf) return NULL;
    /* if the buffer exists, we require '\n' (LF) termination */
    assert(strchr(buf, '\n'));

    if (!strchr(hdr, ':')) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "mgetheader called without : in header tag \"%s\"", hdr);
	abort();
    }

    if (str_isprefix(buf, hdr)) {
	p = buf;
    } else {
	hunt = mastr_new(strlen(hdr) + 1);
	(void)mastr_vcat(hunt, "\n", hdr, NULL);
	p = strstr(buf, mastr_str(hunt));
	mastr_delete(hunt);
	if (!p)
	    return NULL;
    }
    p++;

    /* skip header tag */
    p += strlen(hdr);
    SKIPLWS(p);
    --p;

    /* accomodate folded lines */
    do {
	++p;
	q = strchr(p, '\n');
    } while (q && q[1] && isspace((unsigned char)q[1]));

    if (p && q) {
	int i;
	value = (char *)critmalloc((size_t) (q - p + 1),
				   "Allocating space for header value");
	/*@+loopexec@*/
	for (i = 0; i < q - p; i++) {
	    /* sort of strncpy, replacing LF by space */
	    value[i] = (p[i] == '\n' || p[i] == '\r') ? ' ' : p[i];
	}
	/*@+loopexec@*/
	/* strip trailing whitespace */
	while (i && isspace((unsigned char)*(value + i - 1)))
	    i--;
	value[i] = '\0';
    }
    return value;
}

/**
 * Find a header in an open article file and return it.
 * Copes with folded header lines.
 * NOTE: calls abort() if header does not contain a colon.
 * \return malloc()ed copy of header without tag (caller must free that)
 * or NULL if not found. */
/*@null@*//*@only@*/ char *
fgetheader(
/** file to search for header, may be NULL */
	      /*@null@*/ FILE * f,
/** header to find, must contain a colon, must not be NULL */
	      /*@notnull@*/ const char *header,
/** flag, if set, the file is rewound before and after access */
	      int rewind_file)
{
    char *hdr, *p, *block;
    size_t hlen;

    if (!f)
	return NULL;

    assert(header != NULL);

    if (!strchr(header, ':')) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "fgetheader called without : in header tag \"%s\"", header);
	abort();
    }

    if (rewind_file)
	rewind(f);
    hdr = NULL;
    hlen = strlen(header);

    while ((p = block = getfoldedline(f))) {
	if (!*p) {
	    free(block);
	    break;		/* read only headers */
	}
	if (0 == strncasecmp(p, header, hlen)) {
	    p += hlen;
	    SKIPLWS(p);
	    hdr = critstrdup(p, "fgetheader");
	    free(block);
	    break;
	}
	free(block);
    }
    if (rewind_file)
	rewind(f);
    return hdr;
}

/**
 * Find a header in an article file and return it.
 * Copes with folded header lines.
 * NOTE: calls abort() if header does not contain a colon.
 * \return malloc()ed copy of header without tag (caller must free that)
 * or NULL if not found. */
/*@null@*//*@only@*/ char *
getheader(
/** filename of article to search */
	     /*@notnull@*/ const char *filename,
/** header to search */
	     /*@notnull@*/ const char *header)
{
    FILE *f;
    char *hdr;
    struct stat st;

    if (stat(filename, &st) || !S_ISREG(st.st_mode))
	return NULL;
    if ((f = fopen(filename, "r")) == NULL)
	return NULL;
    hdr = fgetheader(f, header, 0);
    (void)log_fclose(f);
    return hdr;
}

/** destructively parse Xref: header with syntax-checking
 * given memory is overwritten (partial strings will be 0-terminated)
 * *groups will be malloced to hold pointers to group names within xref
 * *artno_strings will be malloced to hold pointers to article numbers
 *  if artno_strings != NULL
 *
 *  \return
 *  - number of groups in case of success
 *  - -1 in case of failure, *groups and *artno_strings are undefined
 */
int
xref_to_list(/*@exposed@*/ char *xref,
	/*@out@*/ char ***groups, /*@null@*/ /*@out@*/ char ***artno_strings,
	int noskip)
{
    char *p, *q;
    int n;
    int max = 20;	/* assume most-catch */
    char **ngs, **nums;

    p = xref;
    if (!noskip) {
	if (!str_isprefix(p, "Xref:"))
	    return -1;
	p += 5;
	SKIPLWS(p);
    }
    SKIPWORD(p);	/* skip hostname */

    ngs = critmalloc(max * sizeof *ngs, "parsekill_xref_line");
    if (artno_strings)
	nums = critmalloc(max * sizeof *nums, "parsekill_xref_line");
    else
	nums = NULL;

    n = 0;
    while (p && *p) {
	ngs[n] = p;
	q = strchr(p, ':');
	if (!q)
	    goto err_out;
	*q++ = '\0';
	if (nums)
	    nums[n] = q;
	q = strchr(q, ' ');
	if (q) {	/* end of line if NULL */
	    *q++ = '\0';
	    SKIPLWS(q);
	}
	p = q;
	if (++n >= max) {
	    max += max;
	    ngs = critrealloc(ngs, max * sizeof *ngs, "parsekill_xref_line");
	    if (nums)
		nums = critrealloc(nums, max * sizeof *nums, "parsekill_xref_line");
	}
    }
    if (n == 0)
	goto err_out;
    *groups = ngs;
    if (artno_strings)
	*artno_strings = nums;
    return n;
err_out:
    free(ngs);
    if (nums)
	free(nums);
    return -1;
}

/**
 * Delete a message in the local news base and in out.going.
 */
void
delete_article(
/** article to kill */
		    const char *mid,
/** "Supersede" or "Cancel" */
		    const char *action,
/** "Superseded" or "Cancelled" */
		    const char *past_action)
{
    const char *filename;
    char *r, *hdr;
    char **ngs, **artnos;
    int n, num_groups;
    struct stat st;
    char **dl, **t;
    int fd = open(".", O_RDONLY);
    char *msgidalloc = critstrdup(mid, "supersede_cancel");
    char *msgid = msgidalloc;

    if (fd < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot determine current "
	       "working directory in supersede_cancel: %m");
	free(msgidalloc);
	return;
    }

    SKIPLWS(msgid);
    if (*msgid != '<')
	goto out;
    if (!(r = strchr(msgid, '>')))
	goto out;
    r[1] = '\0';

    if (debugmode & DEBUG_CANCEL)
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "debug: %s %s", action, msgid);

    filename = lookup(msgid);
    if (!filename)
	goto out;

    hdr = getheader(filename, "Xref:");
    if (!hdr)
	goto out;

    if ((num_groups = xref_to_list(hdr, &ngs, &artnos, 1)) == -1) {
	/* FIXME: diagnostic! */
	free(hdr);
	goto out;
    }

    for (n = 0; n < num_groups; n++) {
	if (debugmode & DEBUG_CANCEL)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "debug %s: xref: \"%s\"", action, ngs[n]);

	if (!chdirgroup(ngs[n], FALSE))
	    continue;		/* no group dir present */

	if (unlink(artnos[n])) {
	    ln_log(errno == ENOENT ? LNLOG_SDEBUG : LNLOG_SERR,
		    LNLOG_CARTICLE,
		    "%s: failed to unlink %s:%s: %m", action, ngs[n], artnos[n]);
	} else {
	    struct newsgroup *g = findgroup(ngs[n], active, -1);
	    ulong u;
	    if (g && get_ulong(artnos[n], &u) && u == g->first)
		g->first++;

	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
		    "%s %s:%s", past_action, ngs[n], artnos[n]);
	}
    }
    free(ngs);
    free(artnos);
    free(hdr);

    /* unlink the message-id hardlink */
    if (stat(filename, &st) && errno != ENOENT) {
	ln_log_sys(LNLOG_SERR, LNLOG_CARTICLE, "cannot stat %s: %m", filename);
    } else {
	if (st.st_nlink > 1) {
	    ln_log_sys(LNLOG_SERR, LNLOG_CARTICLE,
		       "%s error: %s: link count is %ld, "
		       "inode no. %ld, running texpire is advised.",
		       action, filename, (long)st.st_nlink, (long)st.st_ino);
	    /* FIXME: not 64-bit aware */
	}
	if (unlink(filename)) {
	    ln_log_sys(LNLOG_SERR, LNLOG_CARTICLE,
		       "Failed to unlink %s: %m", filename);
	} else {
	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE, "%s %s", past_action, filename);
	}
    }

    /* delete from out.going */
    /* FIXME: also delete from in.coming */
    dl = spooldirlist_prefix("out.going", DIRLIST_ALL, 0);
    if (!dl) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "Cannot read out.going directory: %m");
    } else {
	for (t = dl; *t; t++) {
	    char *x = getheader(*t, "Message-ID:");
	    if (x) {
		if (!strcmp(x, msgid)) {
		    if (0 == log_unlink(*t, 0))
			ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
			       "%s %s", past_action, *t);
		    /* break; *//* commented out to catch all */
		}
		free(x);
	    }
	}
	free_dirlist(dl);
    }

    if (fchdir(fd))
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot restore working directory " "in supersede_cancel: %m");
out:
    (void)log_close(fd);
    free(msgidalloc);
}
