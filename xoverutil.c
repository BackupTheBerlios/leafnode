/*
  xoverutil -- handling xover records
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "get.h"
#include "mastring.h"
#include "bsearch_range.h"
#include "msgid.h"
#include "sgetcwd.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/* global variables are initialized here */
unsigned long xfirst = 0;
unsigned long xlast = 0;
unsigned long xcount = 0;
struct xoverinfo *xoverinfo = NULL;

/* order must match enum xoverfields here! */
static struct {
    const char *header;
    int len;
} xoverentry[] = {
    { "", 1 },
    { "Subject:", 8 },
    { "From:", 5 },
    { "Date:", 5 },
    { "Message-ID:", 11 },
    { "References:", 11 },
    { "Bytes:", 6 },
    { "Lines:", 6 },
    { "Xref:", 5 }
};

/** match header name -> xover entry
 */
enum xoverfields matchxoverfield(const char *header)
{
    enum xoverfields f;
    switch (toupper((unsigned char)header[0])) {
    case 'S': f = XO_SUBJECT;    break;
    case 'F': f = XO_FROM;       break;
    case 'D': f = XO_DATE;       break;
    case 'M': f = XO_MESSAGEID;  break;
    case 'R': f = XO_REFERENCES; break;
    case 'B': f = XO_BYTES;      break;
    case 'L': f = XO_LINES;      break;
    case 'X': f = XO_XREF;       break;
    default: return XO_ERR;
    }
    if (!strncasecmp(header, xoverentry[f].header, xoverentry[f].len))
	return f;
    else
	return XO_ERR;
}

/*@null@*/ /*@only@*/ char *
getxoverfield(char *xoverline, enum xoverfields f)
{
    if (f == XO_ERR || f == XO_ARTNO)
	return NULL;
    else
	return cuttab(xoverline, f+1);
}



/** Extract information from given file to construct an .overview line.
 *  \return a malloc()ed string .overview */
/*@null@*/ /*@only@*/
char *
getxoverline(
	/** if set, delete articles with missing or improper hardlink to message.id */
	const int require_messageidlink,
	/** name of article file */
	const char *const filename)
{
    char *l, *block;
    FILE *f;
    struct stat st;
    struct utimbuf buf;
    char *result = NULL;

    /* We have to preserve atime and mtime to correctly
       expire unsubscribed groups */
    if (lstat(filename, &st)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE, "cannot stat %s: %m", filename);
	return NULL;
    }

    if (st.st_size == 0) {
	/* empty article, probably truncated by store to mark it as broken */
	ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE, "removing empty article file %s",
	       filename);
	log_unlink(filename, 0);
	return NULL;
    }

    buf.actime = st.st_atime;
    buf.modtime = st.st_mtime;

    if ((f = fopen(filename, "r"))) {
	char *from, *subject, *date, *msgid, *references, *xref;
	long bytes, linecount;
	long hbytes = -1, hlines = -1; /* from header */

	from = NULL;
	subject = NULL;
	date = NULL;
	msgid = NULL;
	references = NULL;
	xref = NULL;
	linecount = 0;
	bytes = st.st_size;

	while ((l = block = getfoldedline(f))) {
	    enum xoverfields field;
	    if (!*l) {
		free(block);
		break;
	    }
	    field = matchxoverfield(l);
	    if (field == XO_ERR) {
		free(block);
		continue;
	    }
	    l += xoverentry[field].len;
	    SKIPLWS(l);

	    switch (field) {
	    case XO_FROM:
		if (!from) {
		    from = critstrdup(l, "getxoverline");
		    tab2spc(from);
		}
		break;
	    case XO_SUBJECT:
		if (!subject) {
		    subject = critstrdup(l, "getxoverline");
		    tab2spc(subject);
		}
		break;
	    case XO_DATE:
		if (!date) {
		    date = critstrdup(l, "getxoverline");
		    tab2spc(date);
		}
		break;
	    case XO_MESSAGEID:
		if (!msgid && *l) {
		    msgid = critstrdup(l, "getxoverline");
		    tab2spc(msgid);
		    D(d_stop_mid(msgid));
		}
		break;
	    case XO_REFERENCES:
		if (!references && *l) {
		    references = critstrdup(l, "getxoverline");
		    tab2spc(references);
		}
		break;
	    case XO_BYTES: /* for delaybody */
		if (!get_long(l, &hbytes)) hbytes=-1;
		break;
	    case XO_LINES: /* for delaybody */
		if (!get_long(l, &hlines)) hlines=-1;
		break;
	    case XO_XREF:
		if (!xref && *l) {
		    xref = critstrdup(l, "getxoverline");
		    tab2spc(xref);
		}
	    default:
		;
	    }
	    free(block);
	}

	while ((l = getaline(f))) {
	    linecount++;
	}

	if (from != NULL && date != NULL && subject != NULL && msgid != NULL
	    && bytes) {
	    if (!require_messageidlink || ihave(msgid)) {
		char *p;
		/* only generate message ID if article has a link in
		   message.id */
		result = (char *)critmalloc(strlen(filename) + strlen(subject) + strlen(from) +
					    strlen(date) + strlen(msgid) +
					    (references ? strlen(references) : 0)
					    + 100 + (xref ? strlen(xref) : 0),
					    "computing overview line");
		p = result + sprintf(result, "%s\t%s\t%s\t%s\t%s\t%s\t%ld\t%ld",
			filename, subject, from, date, msgid,
			references ? references : "", max(hbytes,bytes), max(hlines,linecount));
		if (xref) {
		    p = mastrcpy(p, "\tXref: ");
		    (void)mastrcpy(p, xref);
		}
	    }
	}			/* FIXME: if mandatory headers missing, delete offending article */
	fclose(f);
	if (from)
	    free(from);
	if (date)
	    free(date);
	if (subject)
	    free(subject);
	if (msgid)
	    free(msgid);
	if (references)
	    free(references);
	if (xref)
	    free(xref);
    }
    /* restore atime */
    utime(filename, &buf);
    return result;
}

/*
 * compare xover entries
 */
static int
_compxover(const void *a, const void *b)
{
    const struct xoverinfo *la = (const struct xoverinfo *)a;
    const struct xoverinfo *lb = (const struct xoverinfo *)b;

    return (la->artno > lb->artno) - (la->artno < lb->artno);
}

/*
 * return xover record of "article". -1 means failure.
 */
long
findxover(unsigned long article)
{
    struct xoverinfo xoi, *fnd;

    if (xcount == 0)
	return -1;
    xoi.artno = article;

    if (debugmode & DEBUG_XOVER) {	/* check ordering */
	unsigned i;
	for (i = 0; i < xcount - 1; i++) {
	    if (xoverinfo[i].artno > xoverinfo[i + 1].artno) {
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "problem in findxover: xoverinfo[%u] and [%u] "
		       "not in ascending order: %lu > %lu, aborting",
		       i, i + 1, xoverinfo[i].artno, xoverinfo[i + 1].artno);
		abort();	/* bail out */
	    }
	}
    }

    fnd = (struct xoverinfo *)bsearch(&xoi, xoverinfo,
				      xcount, sizeof(struct xoverinfo),
				      _compxover);

    if (!fnd)
	return -1;

    return fnd - xoverinfo;	/* no division here, 
				   address arithmetic does that already */
}

/*
 * get index pair of smallest xoverinfo entry with
 * artno >= low and highest entry with artno <= high
 * returns -1 for failure
 */
int
findxoverrange(unsigned long low, unsigned long high,
	       /*@out@*/ long *idxlow, /*@out@*/ long *idxhigh)
{
    struct xoverinfo xl, xh, *fl, *fh;

    if (xcount == 0 || low > high)
	return -1;

    xl.artno = low;
    xh.artno = high;

    bsearch_range(&xl, &xh, &fl, &fh, xoverinfo, xcount,
		  sizeof(struct xoverinfo), _compxover);
    if (!fl || !fh)
	return -1;
    *idxlow = fl - xoverinfo;
    *idxhigh = fh - xoverinfo;
    return 0;
}

static unsigned long
crunchxover(struct xoverinfo *xi, unsigned long count)
{
    unsigned long i, j;
    i = j = 0;
    while (j < count) {
	while (j < count && xi[j].exists == 0) {
	    if (xi[j].text)
		free(xi[j].text);
	    j++;
	}
	if (j >= count)
	    break;
	if (i != j)
	    xi[i] = xi[j];
	++i;
	++j;
    }
    return i;
}

/**
 * Read xoverfile into struct xoverinfo.
 * \return
 * - 0 problem
 * - 1 success
 */
int
xgetxover(
/** if set, delete articles with missing or improper hardlink to message.id */
	     const int require_messageidlink,
    /** if set, update first/last/count of this group */
	     /*@null@*/ struct newsgroup *g,
	     /** if set, try to update .overview */
	     const int fixxover)
{
    struct stat st;
    char *overview = NULL;
    /*@dependent@*/ char *p, *q;
    char **dl = NULL, **t;		/* directory file list */
    int fd, update = 0;
    unsigned long current, art;
    long i;

    freexover();
    xfirst = ULONG_MAX; /* will be lowered accordingly */
    xlast = 0;		/* will be raised accordingly */
    xcount = 0;

    fd = open(".overview", O_RDONLY);
    if (fd < 0) {
	/* this is debug priority because we remove .overview files from
	 * empty groups */
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
		"cannot read %s/.overview: %s", sgetcwd(),
		strerror(errno));
    } else {
	if (fstat(fd, &st)) {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP,
		    "error: cannot fstat %s/.overview: %s", sgetcwd(),
		    strerror(errno));
	} else {
	    overview = (char *)critmalloc((size_t)st.st_size + 1, "xgetxover");
	    if ((ssize_t)st.st_size != read(fd, overview, st.st_size)) {
		ln_log(LNLOG_SERR, LNLOG_CGROUP,
			"error: cannot read %s/.overview: %s",
			sgetcwd(), strerror(errno));
		free(overview);
		overview = NULL;
	    } else {
		overview[st.st_size] = '\0';
	    }
	}
	close(fd);
    }

    /* count number of entries in .overview file, store into current */
    p = overview;
    current = 0;
    if (p) {
	while (*p) {
	    q = strchr(p, '\n');
	    if (q) {
		p = q + 1;
		current++;
	    } else {
		break;
	    }
	}
    }

    if (fixxover) {
	dl = dirlist(".", DIRLIST_ALLNUM, &xcount);
	if (!dl) {
	    free(overview);
	    return 0;
	}

	/* find article range on disk, store into xcount */
	/* FIXME: don't choke on numeric subgroups */
	/* FIXME: get this under the same roof as nntpd's dogroup */
	for (t = dl; *t; t++) {
	    if (!get_ulong(*t, &art))
		abort();		/* FIXME: must not happen */
	    xcount++;
	    if (art < xfirst)
		xfirst = art;
	    if (art > xlast)
		xlast = art;
	}
    }

    /* parse .overview file */
    xoverinfo = (struct xoverinfo *)critrealloc((char *)xoverinfo,
						sizeof(struct xoverinfo) *
						(xcount + current + 1),
						"allocating overview array");
    memset(xoverinfo, 0, sizeof(struct xoverinfo) * (xcount + current + 1));

    p = overview;
    current = 0;
    while (p && *p) {
	char *tmp;

	SKIPLWS(p);
	q = strchr(p, '\n');
	if (q)
	    *q++ = '\0';
	art = strtoul(p, &tmp, 10);	/* FIXME ... hum, what is to fix here? */
	if (art && tmp && *tmp) {
	    if (fixxover && (art > xlast || art < xfirst)) {
		update = 1;
	    } else {
		char *tmp2 = critstrdup(p, "getxover");
		xoverinfo[current].text = tmp2;
		xoverinfo[current].exists = 0;
		xoverinfo[current].artno = art;
		current++;
		if (art < xfirst) xfirst = art;
		if (art > xlast) xlast = art;
	    }
	}
	p = q;
    }

    xcount = current;		/* to prevent findxover from choking */
    if (!fixxover) {
	if (g)
	    g->count = current;
	free(overview);
	return 1;
    }

    /* this part fixes the overview data */
    /* now fix things, iterate over directory list */
    for (t = dl; *t; t++) {
	(void)get_ulong(*t, &art);	/* would have failed above */
	/* article already known in xover, skip loop body */
	if (overview && ((i = findxover(art)) >= 0)) {
	    xoverinfo[i].exists = 1;
	    continue;
	}

	/* not yet known, see if it can be taken into overview */
	/* check if regular file */
	if (stat(*t, &st)) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP, "Can't stat %s", *t);
	    continue;
	} else if (!S_ISREG(st.st_mode)) {
	    /* not a regular file, skip */
	    continue;
	}

	/* enter new xover line into database */
	if ((xoverinfo[current].text = getxoverline(require_messageidlink, *t))) {
	    xoverinfo[current].exists = 1;
	    xoverinfo[current].artno = art;
	    update = 1;
	    current++;
	} else {
	    char s[LN_PATH_MAX];
	    /* FIXME: use proper article deletion */
	    /* FIXME: don't delete if it's nothing about the article */
	    /* error getting xoverline from article - delete it */
	    (void)getcwd(s, LN_PATH_MAX);
	    if (0 == lstat(*t, &st)) {
		if (S_ISREG(st.st_mode)) {
		    if (unlink(*t)) {
			ln_log(LNLOG_SWARNING, LNLOG_CARTICLE,
			       "could not remove malformatted article %s/%s: %m",
			       s, *t);
		    } else {
			ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
			       "deleted malformatted article: %s/%s", s, *t);
		    }
		} else {
		    ln_log(LNLOG_SWARNING, LNLOG_CARTICLE,
			   "%s/%s is not a regular file", s, *t);
		}
	    }
	}
    }

    if (dl)
	free_dirlist(dl);
    if (overview)
	free(overview);

    /* look for removed articles */
    if (!update) {
	for (art = 0; art < current; art++) {
	    if (xoverinfo[art].text && !xoverinfo[art].exists) {
		update = 1;
		break;		/* no need to go through the rest */
	    }
	}
    }

    /* crunch xover, delete nonexistent articles */
    current = crunchxover(xoverinfo, current);

    /* free superfluous memory */
    /* FIXME: do we need this at all? After all, free will get rid of that */
    if (current) {
	xoverinfo = (struct xoverinfo *)critrealloc((char *)xoverinfo,
						    sizeof(struct xoverinfo) *
						    (current + 1),
						    "reallocating overview array");
	ln_sort(xoverinfo, current, sizeof(struct xoverinfo), _compxover);
    }

    /* sort xover */
    xcount = current;

    if (update)
	writexover();

    if (g) {
	g->first = min(xfirst, g->last + 1);
	if (xlast > g->last)
	    g->last = xlast;
	g->count = current;
    }

    return 1;
}

/*
 * write .overview file
 */
int
writexover(void)
{
    char newfile[] = ".overview.XXXXXX";
    int wfd, err = 0;
    unsigned long art;
    FILE *w;

    if ((wfd = mkstemp(newfile)) == -1) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "mkstemp of new .overview failed: %m");
	return -1;
    }

    w = fdopen(wfd, "w");
    if (!w) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "cannot fdopen new .overview: %m");
	return -1;
    }

    clearerr(w);
    for (art = 0; art < xcount; art++) {
	if (xoverinfo[art].exists && xoverinfo[art].text) {
	    if ((EOF == fputs(xoverinfo[art].text, w))
		|| (EOF == fputs("\n", w))) {
		err = 1;
		break;
	    }
	}
    }

    if (log_fchmod(wfd, (mode_t) 0660)) {
	err = 1;
    }

    if (fclose(w)) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "Cannot write new .overview file: %m");
	err = 1;
    }

    if (!err) {
	if (log_rename(newfile, ".overview"))
	    err = 1;
    }

    if (!err) {
	char s[LN_PATH_MAX];
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	       "wrote %s/.overview", getcwd(s, LN_PATH_MAX));
	return 0;
    } else {
	(void)log_unlink(newfile, 0);
	return -1;
    }
}

void
freexover(void)
{
    if (xoverinfo) {
	long unsigned i;
	for (i = 0; i < xcount; i++) {
	    if (xoverinfo[i].text)
		free(xoverinfo[i].text);
	}
	free(xoverinfo);
	xoverinfo = 0;
    }
    xcount = 0;
}
