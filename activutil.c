/*
  libutil -- deal with active file
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/
#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "format.h"
#include "mastring.h"

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

#ifdef DEBUG_DMALLOC
#include <dmalloc.h>
#endif

#define GROUPINFO "/leaf.node/groupinfo"

size_t activesize = 0;
time_t activetime = 0;
ino_t activeinode = 0;
time_t localmtime = 0;
ino_t localinode = 0;

struct newsgroup *active = NULL;

struct nglist {
    struct newsgroup *entry;
    struct nglist *next;
};

static int
_compactive(const void *a, const void *b)
{
    const struct newsgroup *la = (const struct newsgroup *)a;
    const struct newsgroup *lb = (const struct newsgroup *)b;

    return strcasecmp(la->name, lb->name);
}

struct nglist *newgroup;

/*
 * insert a group into a list of groups. If a group is already present,
 * it will not be inserted again or changed.
 */
void
insertgroup(const char *name, const char status, long unsigned first,
	    long unsigned last, int age, const char *desc)
{
    struct nglist *l;
    static struct nglist *lold;
    struct newsgroup *g;

    if (active) {
	g = findgroup(name);
	if (g)
	    return;
    }
    g = (struct newsgroup *)critmalloc(sizeof(struct newsgroup),
				       "Allocating space for new group");

    g->name = critstrdup(name, "insertgroup");
    g->first = first;
    g->last = last;
    g->count = 0;
    g->age = age;
    g->desc = desc ? critstrdup(desc, "insertgroup") : NULL;
    g->status = status;
    l = (struct nglist *)critmalloc(sizeof(struct nglist),
				    "Allocating space for newsgroup list");

    l->entry = g;
    l->next = NULL;
    if (newgroup == NULL)
	newgroup = l;
    else
	lold->next = l;
    lold = l;
}

/*
 * change description of newsgroup
 */
void
changegroupdesc(const char *groupname, char *description)
{
    struct newsgroup *ng;

    if (groupname && description) {
	ng = findgroup(groupname);
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
	active[count].name = (l->entry)->name;
	active[count].first = (l->entry)->first;
	active[count].last = (l->entry)->last;
	active[count].age = (l->entry)->age;
	active[count].desc = (l->entry)->desc;
	active[count].status = (l->entry)->status;
	free(l->entry);
	l = l->next;
	count++;
	free(la);		/* clean up */
    }
    newgroup = NULL;
    active[count].name = NULL;
    activesize = count;
    sort(active, activesize, sizeof(struct newsgroup), _compactive);
}

/*
 * find a newsgroup in the active file, active must already be read
 */
struct newsgroup *
findgroup(const char *name)
{
    char *n = critstrdup(name, "findgroup");
    struct newsgroup ng = { 0, 0, 0, 0, 0, 0, 0 };
    struct newsgroup *found;

    ng.name = n;
    if (active) {
	found = (struct newsgroup *)bsearch(&ng, active, activesize,
					    sizeof(struct newsgroup),
					    _compactive);
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
void
writeactive(void)
{
    FILE *a;
    struct newsgroup *g;
    struct stat st;
    mastr *c = mastr_new(PATH_MAX);
    mastr *s = mastr_new(PATH_MAX);
    size_t count;
    int err, fd;

    mastr_vcat(s, spooldir, GROUPINFO ".new", 0);
    fd = open(mastr_str(s), O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", mastr_str(s));
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
    count = g - active;
    sort(active, count, sizeof(struct newsgroup), &_compactive);

    /* write groupinfo */
    g = active;
    err = 0;
    while (g->name) {
	if (*(g->name)) {
	    char num[30];
	    /* do error checking at end of loop */
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
	log_unlink(mastr_str(s));
	goto bye;
    }

    if (fstat(fd, &st)) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "cannot fstat %d: %m", fd);
	goto bye;
    }

    log_fclose(a);
    mastr_vcat(c, spooldir, GROUPINFO, 0);
    if (rename(mastr_str(s), mastr_str(c))) {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		   "failed to rename new groupinfo file into place: %m");
    } else {
	/* prevent reloading the just-written groupinfo */
	activeinode = st.st_ino;
	activetime = st.st_mtime;
	ln_log_sys(LNLOG_SINFO, LNLOG_CTOP,
		   "wrote groupinfo with %lu lines.", (unsigned long)count);
    }
  bye:
    mastr_delete(c);
    mastr_delete(s);
}

/*
 * free active list. Written by Lloyd Zusman
 * safe to call without active
 */
void
freeactive(void)
{
    struct newsgroup *g;

    if (active == NULL)
	return;

    g = active;
    while (g->name) {
	free(g->name);
	if (g->desc)
	    free(g->desc);
	g++;
    }
    free(active);
    active = 0;
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
    char s[PATH_MAX];		/* FIXME: possible overrun below */
    char *t;

    freeactive();
    t = mastrcpy(s, spooldir);
    t = mastrcpy(t, GROUPINFO);

    if ((f = fopen(s, "r")) != NULL) {

    } else {
	ln_log_sys(LNLOG_SERR, LNLOG_CTOP, "unable to open %s: %m", s);
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
	if (!r || (*r++ = '\0',
		   sscanf(r, "%c\t%lu\t%lu\t%lu\t", &g->status, &g->last,
			  &g->first, &g->age) != 4)) {
	    ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
		       "Groupinfo file possibly truncated or damaged: %s", p);
	    break;
	}
	g->name = critstrdup(p, "readactive");
	if (g->first == 0)
	    g->first = 1;	/* pseudoarticle */
	if (g->last == 0 && !islocalgroup(g->name))
	    g->last = 1;
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
    activesize = g - active;	/* C magic */
    /* needed so that subsequent insertgroup can work properly */
    sort(active, activesize, sizeof(struct newsgroup), &_compactive);
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
    s2 = (char *)critmalloc(strlen(libdir) + strlen(append) + 1,
			    "rereadactive");
    t = mastrcpy(s1, spooldir);
    mastrcpy(t, GROUPINFO);
    t = mastrcpy(s2, libdir);
    mastrcpy(t, append);

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
