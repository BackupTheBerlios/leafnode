/** \file artutil.c 
 * stuff dealing with articles.
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1998, 1999.
 * Modifications (C) 2001 by Matthias Andree <matthias.andree@gmx.de>
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "mastring.h"
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
	      /*@null@*/ char *buf)
{
    mastr *hunt;
    char *p, *q;
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
    debug = 0;
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
    debug = debugmode;
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

/**
 * Delete a message in the local news base and in out.going.
 */
void
supersede_cancel(
/** article to kill */
		    const char *mid,
/** "Supersede" or "Cancel" */
		    const char *action,
/** "Superseded" or "Cancelled" */
		    const char *past_action)
{
    const char *filename;
    char *p, *q, *r, *hdr;
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
	return;
    if (!(r = strchr(msgid, '>')))
	return;
    r[1] = '\0';

    if (debug & DEBUG_CANCEL)
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "debug: %s %s", action, msgid);

    filename = lookup(msgid);
    if (!filename) {
	(void)close(fd);
	free(msgidalloc);
	return;
    }

    p = hdr = getheader(filename, "Xref:");
    if (!hdr) {
	(void)close(fd);
	free(msgidalloc);
	return;
    }

    /* jump hostname entry */
    while (p && !isspace((unsigned char)*p))
	p++;
    SKIPLWS(p);
    /* now p points to the first newsgroup */

    /* unlink all the hardlinks in the various newsgroups directories */
    while (p && ((q = strchr(p, ':')))) {
	if (debug & DEBUG_CANCEL)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "debug %s: xref: \"%s\"", action, p);
	*q++ = '\0';
	/* p now points to the newsgroup, q to the article number */
	if (chdirgroup(p, FALSE)) {
	    r = p;
	    p = q;
	    /* chop off substring */
	    q = strchr(q, ' ');
	    if (q)
		*q++ = '\0';
	    if (unlink(p)) {
		ln_log(errno == ENOENT ? LNLOG_SNOTICE : LNLOG_SERR,
		       LNLOG_CARTICLE,
		       "%s: failed to unlink %s:%s: %m", action, r, p);
	    } else {
		ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
		       "%s %s:%s", past_action, r, p);
	    }
	}
	p = q;
	if (p)
	    SKIPLWS(p);
    }

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
		    if (0 == log_unlink(*t))
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
    (void)log_close(fd);
    free(msgidalloc);
    free(hdr);
}
