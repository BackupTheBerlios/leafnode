/*
  activutil.c -- deal with active file
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/
#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "format.h"
#include "mastring.h"
#include "activutil.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <fcntl.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define GROUPINFO "/leaf.node/groupinfo"

static size_t oldactivesize = 0;
size_t activesize = 0;
static time_t activetime = 0;
static ino_t activeinode = 0;
static time_t localmtime = 0;
static ino_t localinode = 0;

struct newsgroup /*@null@*/ *active  = NULL;
struct newsgroup /*@null@*/ *oldactive  = NULL;

struct nglist {
    struct newsgroup *entry;
    /*@null@*/ struct nglist *next;
};

int
compactive(const void *a, const void *b)
{
    const struct newsgroup *la = (const struct newsgroup *)a;
    const struct newsgroup *lb = (const struct newsgroup *)b;

    return strcasecmp(la->name, lb->name);
}

void
newsgroup_copy(struct newsgroup *d, const struct newsgroup *s)
    /** note this is not a deep copy, only string pointers are copied,
     * not the strings themselves!
     */
{
	d->name = s->name;
	d->first = s->first;
	d->last = s->last;
	d->count = s->count;
	d->age = s->age;
	d->desc = s->desc;
	d->status = s->status;
}

static /*@null@*/ /*@owned@*/ struct nglist *newgroup;

/*
 * insert a group into a list of groups. If a group is already present,
 * it will not be inserted again or changed.
 */
void
insertgroup(const char *name, char status, long unsigned first,
	    long unsigned last, time_t age, const char *desc)
    /*@globals newgroup@*/
{
    struct nglist *l;
    static /*@dependent@*/ struct nglist *lold;
    struct newsgroup *g;
    unsigned long count = 0ul;

    /* interpret INN status characters x->n, j->y, =->y */
    if (status == 'x') status = 'n';
    if (strchr("j=", status)) status = 'y';

    if (oldactive) {
	g = findgroup(name, oldactive, oldactivesize);
	if (g) {
	    last = g->last;
	    first = g->first;
	    count = g->count;
	    if (g->age) age = g->age;
	    if (g->desc) desc = g->desc;
	}
    }
    
    if (active) {
	g = findgroup(name, active, -1);
	if (g) {
	    g->status = status;
	    if (desc && (g->desc == NULL || strcmp(g->desc, desc) != 0)) {
		if (g->desc) free(g->desc);
		g->desc = critstrdup(desc, "insertgroup");
	    }
	    return;
	}
    }
    g = (struct newsgroup *)critmalloc(sizeof(struct newsgroup),
				       "Allocating space for new group");

    g->name = critstrdup(name, "insertgroup");
    g->first = first;
    g->last = last;
    g->count = count;
    g->age = age;
    g->desc = desc ? critstrdup(desc, "insertgroup") : NULL;
    g->status = status;

    l = (struct nglist *)critmalloc(sizeof(struct nglist),
				    "Allocating space for newsgroup list");

    l->entry = g;
    if (newgroup == NULL) {
	l->next = newgroup;   /* make splint happy */
	newgroup = l;
    } else {
	l->next = lold->next; /* == NULL, appending to the list */
	lold->next = l;
    }
    lold = l;
}

/** Queries the mtime of the active file in memory. \return -1 if no
 * active has been read, time_t mtime otherwise. */
time_t query_active_mtime(void)
{
    if (active && activetime) return activetime;
    return (time_t)-1;
}

/*
 * change description of newsgroup, if the group is not found, nothing happens.
 */
void
changegroupdesc(const char *groupname, char *description)
{
    struct newsgroup *ng;

    if (groupname && description) {
	ng = findgroup(groupname, active, -1);
	if (ng) {
	    if (ng->desc)
		free(ng->desc);
	    ng->desc = critstrdup(description, "changegroupdesc");
	}
    }
}

/*
 * merge nglist with active group, then free it
 */
void
mergegroups(void)
    /*@globals active, newgroup@*/
{
    struct nglist *l, *la;
    size_t count = 0;

    l = newgroup;
    while (l) {
	count++;
	l = l->next;
    }

    active = (struct newsgroup *)critrealloc((char *)active,
					     (1 + count +
					      activesize) *
					     sizeof(struct newsgroup),
					     "reallocating active");
    l = newgroup;
    count = activesize;
    while (l) {
	la = l;
	newsgroup_copy(active + count, l->entry);
	l = l->next;
	count++;
	free(la->entry);
	free(la);		/* clean up */
    }
    newgroup = NULL;
    active[count].name = NULL;
    activesize = count;
    sort(active, activesize, sizeof(struct newsgroup), compactive);
    validateactive();
}

/*
 * find a newsgroup in the active file a, active must already be read.
 * The size of the active file can be passed to asize. If asize == -1
 * activesize will be used.
 */
/*@null@*/ /*@dependent@*/ struct newsgroup *
findgroup(const char *name, struct newsgroup *a, size_t asize)
{
    char *n = critstrdup(name, "findgroup");
    struct newsgroup ng = { 0, 0, 0, 0, 0, 0, 0 };
    struct newsgroup *found;

    ng.name = n;
    if (a) {
	found = (struct newsgroup *)bsearch(&ng, a,
					    (asize == (size_t)-1 ?
					     activesize : asize),
					    sizeof(struct newsgroup),
					    compactive);
    } else {
	ln_log(LNLOG_SCRIT, LNLOG_CTOP,
	       "findgroup(\"%s\") called without prior readactive()", name);
	abort();
    }
    free(n);
    return found;
}

/*
 * write active file
 */
int
writeactive(void)
{
    FILE *a;
    struct newsgroup *g;
    struct stat st;
    mastr *c = mastr_new(LN_PATH_MAX);
    char *tmp;
    size_t count;
    int err, fd;
    int rc = -1;

    if (!active) {
	mastr_delete(c);
	ln_log_sys(LNLOG_SWARNING, LNLOG_CTOP,
		   "warning: writeactive called without "
		   "active file in memory, skipping");
	return -1;
    }

    { /* this block limits the s scope */
	mastr *s = mastr_new(LN_PATH_MAX);
	mastr_vcat(s, spooldir, GROUPINFO ".XXXXXXXXXX", NULL);
	tmp = critstrdup(mastr_str(s), "writeactive");
	mastr_delete(s);
    }

    fd = safe_mkstemp(tmp);
    /* fd = open(mastr_str(s), O_WRONLY | O_CREAT | O_EXCL, 0664); */
    if (fd < 0) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", tmp);
	goto bye;
    }

    if (log_fchmod(fd, 0664)) {
	goto bye;
    }

    a = fdopen(fd, "w");
    if (!a) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "cannot fdopen(%d): %m", fd);
	goto bye;
    }

    /* count members in array and sort it */
    g = active;
    count = 0;
    while (g->name) {
	g++;
    }
    count = activesize = (size_t)(g - active);
    sort(active, count, sizeof(struct newsgroup), &compactive);
    validateactive();

    /* write groupinfo */
    g = active;
    err = 0;
    while (g->name) {
	if (*(g->name)) {
	    char num[30];
	    /* do error checking at end of loop body */
	    fputs(g->name, a);
	    fputc('\t', a);
	    fputc(g->status, a);
	    fputc('\t', a);
	    str_ulong(num, g->last);
	    fputs(num, a);
	    fputc('\t', a);
	    str_ulong(num, g->first);
	    fputs(num, a);
	    fputc('\t', a);
	    str_ulong(num, g->age);
	    fputs(num, a);
	    fputc('\t', a);
	    fputs(g->desc && *(g->desc) ? g->desc : "-x-", a);
	    fputc('\n', a);
	    if ((err = ferror(a)))
		break;
	}
	g++;
    }

    if (err || fflush(a) || fsync(fd)) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		   "failed writing new groupinfo file: %m");
	log_fclose(a);
	log_unlink(tmp, 0);
	goto bye;
    }

    if (fstat(fd, &st)) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "cannot fstat %d: %m", fd);
	goto bye;
    }

    log_fclose(a);
    mastr_vcat(c, spooldir, GROUPINFO, NULL);
    if (rename(tmp, mastr_str(c))) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		   "failed to rename new groupinfo file %s into place: %m",
		   tmp);
    } else {
	/* prevent reloading the just-written groupinfo */
	activeinode = st.st_ino;
	activetime = st.st_mtime;
	ln_log_sys(LNLOG_SINFO, LNLOG_CTOP,
		   "wrote groupinfo with %lu lines.", (unsigned long)count);
	rc = 0;
    }
  bye:
    mastr_delete(c);
    free(tmp);
    return rc;
}

/*
 * free active list. Written by Lloyd Zusman
 * safe to call without active
 */
void
freeactive(/*@null@*/ /*@only@*/ struct newsgroup *a)
{
    struct newsgroup *g;

    if (a == NULL)
	return;

    g = a;
    /*@+loopexec@*/
    while (g->name) {
	free(g->name);
	if (g->desc)
	    free(g->desc);
	g++;
    }
    /*@=loopexec@*/
    free(a);
}

/*
 * read active file into memory
 */
static void readactive(void);
static void
readactive(void)
{
    char *p, *r = 0;
    int n;
    FILE *f;
    struct newsgroup *g;
    mastr *s = mastr_new(LN_PATH_MAX);

    freeactive(active);
    active = 0;
    mastr_vcat(s, spooldir, GROUPINFO, NULL);

    if ((f = fopen(mastr_str(s), "r")) == NULL) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "unable to open %s: %m",
		mastr_str(s));
	mastr_delete(s);
	active = NULL;
	activesize = 0;
	return;
    }

    /* count lines = newsgroups */
    activesize = 0;
    while ((p = getaline(f)))
	activesize++;
    rewind(f);
    active = (struct newsgroup *)critmalloc((1 + activesize) *
					    sizeof(struct newsgroup),
					    "allocating active");
    g = active;
    while ((p = getaline(f))) {
	r = strchr(p, '\t');
	if (!r && sscanf(p, "%*[^ ] %*u %*u %*u %*s")) {
	    ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		       "groupinfo in old format. You MUST run "
		       "fetchnews -f in order to fix this.");
	    abort();
	    /* old format groupinfo, must refresh */
	}
	if (!r
	    || (*r++ = '\0',
		sscanf(r, "%c\t%lu\t%lu\t%lu\t", &g->status, &g->last,
		       &g->first, &g->age) != 4)
	    || !strchr("ymn", g->status)) {
	    ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		       "Groupinfo file damaged, ignoring line: %s", p);
	    /* skip over malformatted line */
	    continue;
	}
	g->name = critstrdup(p, "readactive");
	if (g->first == 0)
	    g->first = 1;	/* pseudoarticle */
	if (g->last == 0 && !is_localgroup(g->name))
	    g->last = 1;
	if (g->last == (unsigned long)-1) {
	    /* corrupt by older leafnode-2 version */
	    ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		       "bogus last value, trying to fix: %s", p);
	    if (chdirgroup(g->name, FALSE))
		(void)getwatermarks(&g->first, &g->last, NULL);
	}
	g->count = 0;
	p = r;
	for (n = 0; n < 4; n++) {	/* Skip the numbers */
	    p = strchr(r, '\t');
	    r = p + 1;
	}
	if (!strcmp(r, "-x-"))
	    g->desc = NULL;
	else
	    g->desc = critstrdup(r, "readactive");
	g++;
    }
    /* last record, to mark end of array */
    g->name = NULL;
    g->first = 0;
    g->last = 0;
    g->age = 0;
    g->desc = NULL;
    g->status = '\0';
    /* count member in the array - maybe there were some empty lines */
    g = active;
    activesize = 0;
    while (g->name) {
	g++;
    }
    activesize = (size_t)(g - active);	/* C magic */
    /* needed so that subsequent insertgroup can work properly */
    sort(active, activesize, sizeof(struct newsgroup), &compactive);
    validateactive();

    /* don't check for errors, we opened the file for reading */
    (void)fclose(f);
    mastr_delete(s);
}

/* only read active if it has changed or not been loaded previously */
void
rereadactive(void)
{
    static struct stat st1, st2;
    char *s1, *s2;
    char *t;
    int reread = 0;
    const char *const append = "/local.groups";

    s1 = (char *)critmalloc(strlen(spooldir) + strlen(GROUPINFO) + 1,
			    "rereadactive");
    s2 = (char *)critmalloc(strlen(sysconfdir) + strlen(append) + 1,
			    "rereadactive");
    t = mastrcpy(s1, spooldir);
    (void)mastrcpy(t, GROUPINFO);
    t = mastrcpy(s2, sysconfdir);
    (void)mastrcpy(t, append);

    if (!active)
	reread = 1;
    if (stat(s1, &st1)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot stat %s: %m, may cause bad performance", s1);
	reread = 1;
    }
    if (stat(s2, &st2)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot stat %s: %m, may cause bad performance", s2);
	reread = 1;
    }

    if (reread || ((st1.st_mtime > activetime)
		   || (st1.st_ino != activeinode))
	|| ((st2.st_mtime > localmtime)
	    || (st2.st_ino != localinode))) {
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "%sreading %s and %s",
	       active ? "re" : "", s1, s2);
	readactive();
	readlocalgroups();
	activetime = st1.st_mtime;
	activeinode = st1.st_ino;
	localmtime = st2.st_mtime;
	localinode = st2.st_ino;
    }
    free(s2);
    free(s1);
}

/*
 * Merge newly read active with old active. Do not reset watermarks and
 * timestamps. (c) 2002 Joerg Dietrich
 */
void mergeactives(struct newsgroup *old, struct newsgroup *newa)
{
    struct newsgroup *g;
    struct newsgroup *ogrp;

    g = newa;
    while (g->name) {
	if ( (ogrp = findgroup(g->name, old, oldactivesize)) != NULL ) {
	    g->first = (g->first > ogrp->first) ? ogrp->first : g->first;
	    g->last = (g->last > ogrp->last) ? g->last : ogrp->last;
	    g->age = (g->age > ogrp-> age) ? g->age : ogrp->age;
	} else {
	    g->age = time(NULL);
	}
	g++;
    }
    g = active = newa;
    activesize = 0;
    while (g->name) {
	activesize++;
	g++;
    }
}

/*
 * Duplicate active file. Return pointer to copy. Does not duplicate the
 * pointers in the newsgroup structure!
 * (c) 2002 Joerg Dietrich
 */
/*@null@*/ struct newsgroup *mvactive(/*@null@*/ struct newsgroup *a)
{
    static struct newsgroup *b;

    oldactivesize = activesize;
    if (a == NULL) return NULL;

    b = (struct newsgroup *)critmalloc((1+activesize) *
				       sizeof(struct newsgroup),
				       "allocating active copy");
    (void)memcpy(b, a, (1+activesize) * sizeof(struct newsgroup));
    active = NULL;
    activesize = 0;
    return b;
}
