/**
 * \file filterutil.c
 *
 * Read and parse filter file and do filtering of messages.
 * (C) 1998 by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
 * modifications (C) 2001 - 2002 by Matthias Andree.
 * modifications (C) 2002 by Ralf Wildenhues.
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/*@null@*/ /*@owned@*/ struct filterlist *filter = NULL;

static void free_entry(/*@null@*/ /*@only@*/ struct filterentry *e);

static enum state { RF_WANTNG, RF_WANTPAT,
		    RF_WANTNGORPAT, RF_WANTACTION } state;

static /*@observer@*/ const char *whatexpected(enum state s);

/*
 * find "needle" in "haystack" only if "needle" is at the beginning of a line
 * returns a pointer to the first char after "needle" which should be a
 * whitespace
 */
static /*@null@*/ /*@dependent@*/ const char *
findinheaders(const char *needle, const char *haystack)
{
    static const char *p;
    const char *q;

    p = haystack;
    while (p && *p) {
	if (strncasecmp(needle, p, strlen(needle)) == 0) {
	    p += strlen(needle);
	    if (isspace((unsigned char)*p))
		return p;
	} else {
	    q = p;
	    p = strchr(q, '\n');
	    if (p && *p)
		p++;
	}
    }
    return NULL;
}

static struct m2n {
    const char *text;
    int mon;
} mon2num[] = {
    {
    "apr", 3}, {
    "aug", 7}, {
    "dec", 11}, {
    "feb", 1}, {
    "jan", 0}, {
    "jul", 6}, {
    "jun", 5}, {
    "mar", 2}, {
    "may", 4}, {
    "nov", 10}, {
    "oct", 9}, {
    "sep", 8}
};

static int
compmon(const void *key, const void *member)
{
    const char *k = (const char *)key;
    const char *m = ((const struct m2n *)member)->text;

    return strcmp(k, m);
}

/*
 * calculate date in seconds since Jan 1st 1970 from a Date: header
 */
static int
age(/*@null@*/ const char *date)
{
    /* these are for struct tm and thus the numbers are 1 low */
    char monthname[4];
    int month;
    int year;
    int day;
    const char *d;
    time_t tmp;
    struct tm time_struct;

    if (!date)
	return 1000;		/* large number: OLD */
    d = date;
    if (strncmp(date, "Date:", 5) == 0)
	d += 5;
    SKIPLWS(d);
    /* skip "Mon" or "Tuesday," and trailing whitespace */
    while (*d == ',' || isalpha((unsigned char)*d))
	d++;
    SKIPLWS(d);

    day = strtol(d, NULL, 10);
    while (isdigit((unsigned char)*d))
	d++;
    SKIPLWS(d);
    if (!isalpha((unsigned char)*d)) {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE, "Unable to parse %s", date);
	return 1003;
    }
    monthname[0] = tolower((unsigned char)*d++);
    monthname[1] = tolower((unsigned char)*d++);
    monthname[2] = tolower((unsigned char)*d++);
    monthname[3] = '\0';
    if (strlen(monthname) != 3) {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
	       "Unable to parse month in %s", date);
	return 1004;
    }
    while (isalpha((unsigned char)*d))
	d++;
    SKIPLWS(d);

    year = strtol(d, NULL, 10);
    if ((year < 1970) && (year > 99)) {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE, "Unable to parse year in %s", date);
	return 1005;
    } else if (!(day > 0 && day < 32)) {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE, "Unable to parse day in %s", date);
	return 1006;
    } else {
	struct m2n *m;
	m = bsearch(monthname, mon2num, 12, sizeof(mon2num[0]), compmon);
	if (!m) {
	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE, "Unable to parse %s", date);
	    return 1001;
	}
	month = m->mon;
	if (year < 70)
	    /* years 2000-2069 in two-digit form */
	    year += 100;
	else if (year > 1970)
	    /* years > 1970 in four-digit form */
	    year -= 1900;
	/* FIXME: read time zone */
	memset(&time_struct, 0, sizeof(time_struct));
	time_struct.tm_sec = 0;
	time_struct.tm_min = 0;
	time_struct.tm_hour = 0;
	time_struct.tm_mday = day;
	time_struct.tm_mon = month;
	time_struct.tm_year = year;
	time_struct.tm_isdst = 0;
	tmp = mktime(&time_struct);
	if (tmp == -1)
	    return 1002;
	return ((time(NULL) - tmp) / SECONDS_PER_DAY);
    }
}

/*
 * create a new filterentry for a list
 */
static /*@only@*/ struct filterlist *
newfilter(void)
{
    struct filterlist *fl;
    struct filterentry *fe;
    fe = (struct filterentry *)critmalloc(sizeof(struct filterentry),
					  "Allocating filterentry space");

    fe->newsgroup = NULL;
    fe->cleartext = NULL;
    fe->expr = NULL;
    fe->action = NULL;
    fe->limit = -1;
    fl = (struct filterlist *)critmalloc(sizeof(struct filterlist),
					 "Allocating filterlist space");

    fl->next = NULL;
    fl->entry = fe;
    return fl;
}

static struct filterlist *oldf = NULL;

static void
insertfilter(/*@owned@*/ struct filterlist *f, /*@only@*/ char *ng)
{
    (f->entry)->newsgroup = ng;
    if (!filter)
	filter = f;
    else
	oldf->next = f;
    oldf = f;
}

static const struct expect
{
    const enum state state;
    /*@observer@*/ const char *msg;
} expect[] = {
    { RF_WANTNG, "newsgroup" },
    { RF_WANTPAT, "pattern" },
    { RF_WANTNGORPAT, "newsgroup or pattern" },
    { RF_WANTACTION, "action" }
};

static /*@observer@*/ const char *
whatexpected(enum state s) {
    const char *x = 0;
    unsigned int i;

    for (i = 0; i < sizeof(expect)/sizeof(expect[0]); i++) {
	if (expect[i] . state == s) x = expect[i] . msg;
    }
    return x;
}

/*
 * read filters into memory. Filters are just plain regexp's
 * return TRUE for success, FALSE for failure
 *
 * in addition, we initialize for standard regular expressions
 */
int
readfilter(/*@null@*/ const char *filterfilename)
    /*@globals undef filter@*/
{
    FILE *ff;
    char *l;
    char *ng = NULL;
    char *param, *value;
    const char *regex_errmsg;
    int regex_errpos;
    struct filterlist *f;
    int rv = TRUE;
    unsigned long line = 0;

    filter = NULL;
    if (!filterfilename || !strlen(filterfilename))
	return FALSE;

    param = (char *)critmalloc(TOKENSIZE, "allocating space for parsing");
    value = (char *)critmalloc(TOKENSIZE, "allocating space for parsing");
    f = NULL;
    ff = fopen(filterfilename, "r");
    if (!ff) {
	ln_log(LNLOG_SWARNING, LNLOG_CTOP,
	       "Unable to open filterfile %s: %m", filterfilename);
	free(param);
	free(value);
	return FALSE;
    }

    /* we manually hack up a state machine here to make reading the
     * filter file robust and get proper error messages.
     *
     * we have four states: RF_WANTNG, RF_WANTPAT, RF_WANTNGORPAT,
     * RF_WANTACTION, which indicate what condition (input line) leads
     * to a transition.
     *
     * the initial state is RF_WANTNG, and the terminal state must be
     * RF_WANTNG or RF_WANTNGORPAT, otherwise the file is incomplete.
     */
    state = RF_WANTNG;
    while ((l = getaline(ff))) {
	line++;
	if (!*l || *l == '#') continue;
	if (parse_line(l, param, value)) {
	    if ((state == RF_WANTNG || state == RF_WANTNGORPAT) &&
		(!strcasecmp("newsgroup", param)
		 || !strcasecmp("newsgroups", param))) {
		if (ng) free(ng);
		ng = critstrdup(value, "readfilter");
		state = RF_WANTPAT;
	    } else if ((state == RF_WANTPAT || state == RF_WANTNGORPAT) &&
		!strcasecmp("pattern", param)) {
		pcre *re;
		if (!ng) {
		    ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
			   "No newsgroup for pattern = %s (line %lu) found",
			   value, line);
		    rv = FALSE;
		    /* strictly speaking, the following "continue"
		     * statement is not needed. We will never be in this
		     * branch with ng == NULL, because we start off in
		     * state RF_WANTNG, and when we get into RF_WANTPAT
		     * or WANTNGORPAT, ng != NULL holds.
		     */
		    continue;
		}
		re = pcre_compile(value, PCRE_MULTILINE,
				  &regex_errmsg, &regex_errpos,
#ifdef NEW_PCRE_COMPILE
				  NULL
#endif
		    );
		if (re) {
		    f = newfilter();
		    insertfilter(f, critstrdup(ng, "readfilter"));
		    (f->entry)->expr = re;
		    (f->entry)->cleartext = critstrdup(value, "readfilter");
		} else {
		    /* could not compile */
		    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
			   "%s: invalid pattern at line %lu: %s",
			   filterfilename, line,
			   regex_errmsg);
		    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
			   "%s: \"%s\"", filterfilename, value);
		    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
			   "%s:  %*s",
			   filterfilename,
			   regex_errpos + 1, "^");
		    rv = FALSE;
		}
		state = RF_WANTACTION;
	    } else if ((state == RF_WANTPAT || state == RF_WANTNGORPAT) && (
			   (!strcasecmp("maxage", param)) ||
			   (!strcasecmp("minlines", param)) ||
			   (!strcasecmp("maxlines", param)) ||
			   (!strcasecmp("maxbytes", param)) ||
			   (!strcasecmp("maxcrosspost", param)))) {
		f = newfilter();
		insertfilter(f, critstrdup(ng, "readfilter"));
		(f->entry)->cleartext = critstrdup(param, "readfilter");
		(f->entry)->limit = (int)strtol(value, NULL, 10);
		state = RF_WANTACTION;
	    } else if (state == RF_WANTACTION
		       && !strcasecmp("action", param)) {
		if (!f || !f->entry || !(f->entry)->cleartext) {
		    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
			   "%s: No pattern found for action %s",
			   filterfilename, value);
		    rv = FALSE;
		} else {
		    (f->entry)->action = critstrdup(value, "readfilter");
		}
		state = RF_WANTNGORPAT;
	    } else {
		ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		       "%s: line %lu: expected %s, saw \"%s = %s\"",
		       filterfilename, line, whatexpected(state),
		       param, value);
		if (state != RF_WANTNG) state = RF_WANTNGORPAT;
		rv = FALSE;
	    }
	} else {
	    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		   "%s: Cannot parse line %lu: \"%s\"", filterfilename,
		   line, l);
	    rv = FALSE;
	}
    }
    (void)fclose(ff);
    if (ng)
	free(ng);
    free(param);
    free(value);
    if (state != RF_WANTNG && state != RF_WANTNGORPAT) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "%s: premature end of file, expected %s",
	       filterfilename, whatexpected(state));
	rv = FALSE;
    }

    if (filter == NULL) {
	ln_log(LNLOG_SWARNING, LNLOG_CTOP,
	       "filterfile %s did not contain any valid patterns",
	       filterfilename);
    }
    return rv;
}

/*
 * create a new filterlist with filters matching the current newsgroup
 */
struct filterlist *
selectfilter(const char *groupname)
{
    struct filterlist *master;
    struct filterlist *f, *fold, *froot;

    froot = NULL;
    fold = NULL;
    master = filter;
    while (master) {
	if (ngmatch((master->entry)->newsgroup, groupname) == 0) {
	    f = (struct filterlist *)critmalloc(sizeof(struct filterlist),
						"Allocating groupfilter space");

	    f->entry = master->entry;
	    f->next = NULL;
	    if (froot == NULL)
		froot = f;
	    else
		fold->next = f;
	    fold = f;
	    if (debugmode & DEBUG_FILTER) {
		ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
		       "filtering in %s: %s -> %s",
		       groupname, (f->entry)->cleartext, (f->entry)->action);
	    }
	}
	master = master->next;
    }
    return froot;
}

/*
 * read and filter headers.
 * Return true if article should be killed, false if not
 */
int
killfilter(const struct filterlist *f, const char *hdr)
{
    int match, score;
    struct filterentry *g;
    const char *p;

    if (!f) {
         if (debugmode & DEBUG_FILTER) {
             ln_log(LNLOG_SDEBUG, LNLOG_CALL, "killfilter: no filter list");
         }
         return FALSE;
    }
    score = 0;
    match = -1;
    for (; f; f = f->next) {
	g = f->entry;
	if (debugmode & DEBUG_FILTER) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CALL, "killfilter: trying filter for %s",
	           g->newsgroup);
	}
	if ((g->limit == -1) && (g->expr)) {
	    match = pcre_exec(g->expr, NULL, hdr, (int)strlen(hdr),
#ifdef NEW_PCRE_EXEC
			      0,
#endif
			      0, NULL, 0);
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "regexp filter: /%s/ returned %d", g->cleartext, match);
	    }
	} else if (strcasecmp(g->cleartext, "maxage") == 0) {
	    long a;
	    p = findinheaders("Date:", hdr);
	    while (p && *p && isspace((unsigned char)*p))
		p++;
	    if ((a = age(p)) > g->limit)
		match = 0;	/* limit has been hit */
	    else
		match = PCRE_ERROR_NOMATCH;	/* don't match by default */
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "maxage filter: age is %ld, limit %ld, returned %d", a,
	               g->limit, match);
	    }
	} else if (strcasecmp(g->cleartext, "maxlines") == 0) {
	    long l = -1;
	    p = findinheaders("Lines:", hdr);
	    if (p) {
		if ((l = strtol(p, NULL, 10)) > g->limit)
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "maxlines filter: lines is %ld, limit %ld, returned %d",
	               l, g->limit, match);
	    }
	} else if (strcasecmp(g->cleartext, "minlines") == 0) {
	    long l = -1;
	    p = findinheaders("Lines:", hdr);
	    if (p) {
		if ((l = strtol(p, NULL, 10)) < g->limit)
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "minlines filter: lines is %ld, limit %ld, returned %d",
	               l, g->limit, match);
	    }
	} else if (strcasecmp(g->cleartext, "maxbytes") == 0) {
	    long l = -1;
	    p = findinheaders("Bytes:", hdr);
	    if (p) {
		if ((l = strtol(p, NULL, 10)) > g->limit)
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "maxbytes filter: bytes is %ld, limit %ld, returned %d",
	               l, g->limit, match);
	    }
	} else if (strcasecmp(g->cleartext, "maxcrosspost") == 0) {
	    long l = 1;
	    char *r, *q;

	    r = q = mgetheader("Newsgroups:", hdr);
	    if (q) {
		while (*q && *q != '\n') {
		    if (*q++ == ',') {
			SKIPLWS(q);
			l++;
		    }
		}
		free(r);
	    }
	    if (l > g->limit)
		match = 0;
	    else
		match = PCRE_ERROR_NOMATCH;
	    if (debugmode & DEBUG_FILTER) {
	        ln_log(LNLOG_SDEBUG, LNLOG_CALL,
	               "maxcrosspost filter: newsgroups %ld, limit %ld, "
	               "returned %d", l, g->limit, match);
	    }
	}

	if (match == 0) {
	    /* this should have been caught by readfilter */
	    if (!g->action) internalerror();

	    /* article matched pattern/limit: what now? */
	    if (strcasecmp(g->action, "select") == 0) {
		return FALSE;
	    } else if (strcasecmp(g->action, "kill") == 0) {
		return TRUE;
	    } else {
		score += strtol(g->action, NULL, 10);
	    }
	}
    }
    if (score < 0)
	return TRUE;
    else
	return FALSE;
}

/*
** Free filterlist but not filterentries
 */
void
freefilter(/*@null@*/ /*@only@*/ struct filterlist *f)
{
    struct filterlist *g;

    while (f) {
	g = f->next;
	free(f);
	f = g;
    }
}

static void
free_entry(/*@null@*/ /*@only@*/ struct filterentry *e)
{
    if (e) {
	if (e->expr)
	    pcre_free(e->expr);
	if (e->action)
	    free(e->action);
	if (e->cleartext)
	    free(e->cleartext);
	if (e->newsgroup)
	    free(e->newsgroup);
	free(e);
    }
}

void
freeallfilter(/*@null@*/ /*@only@*/ struct filterlist *f)
{
    struct filterlist *g;
    while (f) {
	g = f->next;
	free_entry(f->entry);
	free(f);
	f = g;
    }
}
