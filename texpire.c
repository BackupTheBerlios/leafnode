/*
texpire -- expire old articles

Written by Arnt Gulbrandsen <agulbra@troll.no> and copyright 1995
Troll Tech AS, Postboks 6133 Etterstad, 0602 Oslo, Norway, fax +47
22646949.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
and Randolf Skerka <Randolf.Skerka@gmx.de>.
Copyright of the modifications 1997.
Modified by Kent Robotti <robotti@erols.com>. Copyright of the
modifications 1998.
Modified by Markus Enzenberger <enz@cip.physik.uni-muenchen.de>.
Copyright of the modifications 1998.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
Copyright of the modifications 1998, 1999.
Modified by Kazushi "Jam" Marukawa <jam@pobox.com>.
Copyright of the modifications 1998, 1999.
Modified by Joerg Dietrich <joerg@dietrich.net>.
Copyright of the modifications 1999.
Modified by Stefan Wiens <s.wi@gmx.net>.
Copyright of the modifications 2001.

See README for restrictions on the use of this software.
*/

/* FIXME: scan for directories not listed in groupinfo (is this what
 * texpire iterates over to expire?) and add them to allow them to be
 * expired.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "format.h"

#ifdef SOCKS
#include <socks.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define SUBJECT 1
#define FROM 2
#define DATE 3
#define MESSAGEID 4
#define REFERENCES 5
#define BYTES 6
#define LINES 7
#define XREF 8


int debug = 0;

static int dryrun = 0;		/* do not delete articles */
static int use_atime = 1;	/* look for atime on articles to expire */
static int repair_spool = 0;

static char gdir[PATH_MAX];		/* name of current group directory */
static unsigned long deleted, kept;

extern unsigned long xcount;

/* Thread expiry by Stefan Wiens 2001-01-08 */

/*
 * All Message-IDs in the Message-ID: and References: XOVER field of an
 * article together are considered as a thread. The first encountered
 * article starts an initial thread.
 * For all subsequent articles, we check if any of its IDs is member of an
 * already known thread. Then we put together all subthreads this article
 * is a member of.
 * After all articles have been considered, we have a list of all threads
 * in this group.
 * Each thread is then searched for an article that has been created/read
 * more recently than the expiry limit. If one exists, the entire thread
 * is rescued from expiry. (Thanks to Joerg Dietrich for inspiration.)
 *
 * "threadlist" is the origin of a linked list of threads. Each element of
 * this list points to a list of Message-IDs known to be members of this
 * thread. Each Message-ID element (rnode) itself has a pointer to its
 * thread list element, so we can easily determine which thread it
 * belongs to.
 * For quick lookup by Message-ID, each of those Message-ID elements
 * (rnode) is also part of a list which contains all IDs with the same
 * hash value. These hash lists are pointed to by a hash table.
 */

struct rnode {
    struct rnode *nhash;	/* next rnode in this hash list */
    struct rnode *nthread;	/* next rnode in this thread */
    struct thread *fthread;	/* start of this thread */
    unsigned long artno;	/* article number, 0 if unknown */
    const char *mid;		/* Message-ID */
};

struct thread {
    struct thread *next;	/* next thread in list */
    struct rnode *subthread;	/* first rnode in this thread */
};

#define HASHSIZE 12345

struct rnode *hashtab[HASHSIZE];	/* each entry points to a list of */
					/* rnodes with same hash value */

static unsigned long hashval(const struct rnode *node);
static void hash_thread(struct thread *thread);
static struct rnode *newnode(const char *mid, unsigned long artno);
static struct rnode *findnode(const struct rnode *node);
static void merge_threads(struct thread *a, struct thread *b);
static struct thread *xoverthread(char *xoverline, unsigned long artno);
static struct thread *build_threadlist(unsigned long acount);
static void free_threadlist(struct thread *);
static unsigned long count_threads(struct thread *);
static void remove_newer(struct thread *, time_t);
static void delete_article(struct rnode *r);
static void delete_threads(struct thread *);
static unsigned long low_wm(unsigned long high);


/* very simple hash function ;-) */
static unsigned long
hashval(const struct rnode *node)
{
    unsigned long val;
    const char *p;
    int i;

    val = 0;
    p = node->mid;
    for (i = 0; i < 20 && p && *p; ++i) {
	val += val ^ (i + (int)(unsigned char)*p++);
    }
    return (val % HASHSIZE);
}

/* put all references in this thread into hash table */
void
hash_thread(struct thread *th)
{
    struct rnode *r;
    unsigned long h;

    r = th->subthread;
    while (r) {
	h = hashval(r);		/* no need to check for duplicates ;-) */
	r->nhash = hashtab[h];
	hashtab[h] = r;		/* push on list */
	r = r->nthread;
    }
}

/* create a new reference node */
struct rnode *
newnode(const char *mid, unsigned long artno)
{
    struct rnode *newn;

    newn = (struct rnode *)critmalloc(sizeof(struct rnode),
				      "Allocating newnode reference");
    newn->nhash = newn->nthread = NULL;
    newn->fthread = NULL;
    newn->artno = artno;
    newn->mid = mid;
    return newn;
}

/* find node with same message-ID, return node or NULL if not found */
struct rnode *
findnode(const struct rnode *node)
{
    struct rnode *f;

    if (!*(node->mid)) {
	return NULL;
    }
    f = hashtab[hashval(node)];
    while (f) {
	if (strcmp(f->mid, node->mid) == 0) {
	    return f;
	}
	f = f->nhash;		/* try next in list */
    }
    return NULL;
}

/* merge thread b into a */
void
merge_threads(struct thread *a, struct thread *b)
{
    struct rnode *r;

    if (!(r = b->subthread)) {	/* nothing to do */
	return;
    }
    while (r->fthread = a,	/* update start of thread pointer */
	   r->nthread) {	/* for all references in thread */
	r = r->nthread;
    }				/* r now points to the last reference in b */
    r->nthread = a->subthread;	/* now link b in front of a */
    a->subthread = b->subthread;
    b->subthread = NULL;	/* FIXME: the now empty thread b */
    /* could immediately be removed from threadlist */
}

/* 
 * return a thread built from an XOVER line,
 * containing its Message-ID and all references
 */
struct thread *
xoverthread(char *xoverline, unsigned long artno)
{
    int i;
    char *p, *q, *r;
    struct thread *newthread;
    struct rnode *node;

    p = xoverline;
    if (!p || !*p || !artno) {	/* illegal */
	return NULL;
    }
    node = newnode("", artno);	/* make this the start of a new thread */
    newthread = (struct thread *)critmalloc(sizeof(struct thread),
					    "Allocating new thread");
    newthread->next = NULL;
    newthread->subthread = node;
    node->fthread = newthread;
    for (i = 0; i < MESSAGEID; ++i) {
	if (!(p = strchr(p, '\t'))) {	/* find Message-ID field */
	    return newthread;	/* cope with broken articles */
	}
	*p++ = '\0';
    }
    if (!(r = strchr(p, '\t'))) {
	return newthread;
    }
    *r++ = '\0';		/* start of References */
    if (!(p = strchr(p, '<')) || !(q = strchr(p, '>'))) {
	return newthread;
    }				/* p now is a valid Message-ID */
    *++q = '\0';		/* zero terminate Message-ID */
    node->mid = p;
    p = q = r;			/* start of References: field */
    if (!(r = strchr(r, '\t'))) {	/* end of references */
	return newthread;
    }
    *r = '\0';
    while (*q && q < r && (q = strchr(q, '>'))) {
	*++q = '\0';		/* zero terminate this reference */
	if ((p = strrchr(p, '<'))) {
	    node = newnode(p, 0);	/* unknown artno */
	    node->nthread = newthread->subthread;
	    node->fthread = newthread;	/* put into this thread */
	    newthread->subthread = node;
	}
	p = ++q;
    }
    return newthread;
}

/*
 * generate threadlist from xoverinfo
 */
struct thread *
build_threadlist(unsigned long acount)
{
    unsigned long i;
    struct thread *x, *firstfound;
    struct thread *tl = 0;
    struct rnode *f, *r, **u = 0;

    for (i = 0; i < acount; ++i) {
	if (xoverinfo[i].artno
	    && xoverinfo[i].text
	    && xoverinfo[i].exists
	    && (x = xoverthread(xoverinfo[i].text, xoverinfo[i].artno))) {
	    u = &(x->subthread);	/* needed for punching out elements */
	    r = *u;
	    firstfound = NULL;
	    while (r) {
		if ((f = findnode(r))) {	/* is this MID already known? */
		    if (r->artno) {
			if (f->artno == 0) {	/* possibly update artno */
			    f->artno = r->artno;
			} else if (f->artno != r->artno) {	/* duplicate */
			    delete_article(r);
			}
		    }
		    if (firstfound) {	/* link subthreads */
			if (f->fthread != firstfound) {	/* not merged yet? */
			    merge_threads(firstfound, f->fthread);
			}
		    } else {	/* this thread everything will go into */
			firstfound = f->fthread;
		    }
		    *u = r->nthread;	/* remove this element from thread */
		    free(r);
		} else {
		    u = &(r->nthread);
		}
		r = *u;
	    }
	    hash_thread(x);
	    if (firstfound) {	/* this article is part of another thread */
		merge_threads(firstfound, x);
		free(x);
	    } else {		/* entirely new thread */
		x->next = tl;
		tl = x;
	    }
	}
    }
    return tl;
}

/* free all rnodes and threads, empty hash table */
void
free_threadlist(struct thread *threadlist)
{
    unsigned long i;
    struct rnode *r;
    struct thread *t;

    for (i = 0; i < HASHSIZE; ++i) {
	while ((r = hashtab[i])) {
	    hashtab[i] = r->nhash;
	    free(r);
	}
    }
    while ((t = threadlist)) {
	threadlist = t->next;
	free(t);
    }
}

/* return number of threads in threadlist */
static unsigned long
count_threads(struct thread *t)
{
    unsigned long n = 0;

    n = 0;

    for (; t; t = t->next)
	if (t->subthread)
	    ++n;

    return n;
}

/** remove all threads from expire list that have at least one new member
 * assumes its cwd is the appropriate news group directory
 */
static void
remove_newer(struct thread *threadlist, time_t expire)
{
    struct thread *t;

    for (t = threadlist; t; t = t->next) {
	struct rnode *r;
	for (r = t->subthread; r; r = r->nthread) {
	    if (r->artno) {
		char name[40];
		struct stat st;

		str_ulong(name, r->artno);
		if ((0 == stat(name, &st))
		    && S_ISREG(st.st_mode)
		    && ((use_atime ? st.st_atime : st.st_mtime)
			> expire)) {
		    /* a newer article was found, disconnect from
		       threadlist.  we later free() from hashtab[] */
		    t->subthread = NULL;
		    break;
		    /* no need to look further */
		}
	    }
	}
    }
}

/** delete article file which belongs to this node.
 * r may be null.
 * this function does nothing if the artno of this node is zero. */
static void
delete_article(struct rnode *r)
{
    char name[PATH_MAX];

    if (!r || !r->artno) {
	return;
    }
    str_ulong(name, r->artno);
    if (dryrun) {
	ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
	       "dry-run: should delete %s/%lu", gdir, r->artno);
	deleted++;
    } else {
	if (!unlink(name)) {
	    if (debugmode & DEBUG_EXPIRE) {
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "deleted article %s/%lu", gdir, r->artno);
	    }
	    r->artno = 0;
	    deleted++;
	} else if (errno != ENOENT && errno != EEXIST) {
	    /* if file was deleted already or it was not a file */
	    /* but a directory, skip error message */
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "unlink %s/%lu: %m", gdir, r->artno);
	}
    }
}

/** delete all article files in all remaining threads */
void
delete_threads(struct thread *threadlist)
{
    struct thread *t;
    struct rnode *r;

    for (t = threadlist; t; t = t->next)
	for (r = t->subthread; r; r = r->nthread)
	    delete_article(r);
}

/** make sure group directory is consistent */
static void
updatedir(const char *groupname)
{
    struct rnode *r;
    char name[PATH_MAX];
    struct stat st, st2;
    const char *m;
    long i;

    if (debugmode & DEBUG_EXPIRE)
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP, "%s: enter updatedir", groupname);

    for (i = 0; i < HASHSIZE; ++i) {
	r = hashtab[i];
	for (r = hashtab[i]; r; r = r->nhash) {
	    if (r->artno && r->mid) {
		str_ulong(name, r->artno);
		if (!stat(name, &st)
		    && S_ISREG(st.st_mode)) {
		    int relink = 0;

		    /* check if message.id is the same file */
		    m = lookup(r->mid);
		    if (stat(m, &st2)) {
			if (errno == ENOENT) {
			    /* message.id file missing, kill off
			     * article, since it has been left behind by
			     * a crashed store() */
			    if (debug & DEBUG_EXPIRE)
				ln_log
				    (LNLOG_SDEBUG,
				     LNLOG_CARTICLE,
				     "%s: article %lu has no "
				     "message.id file", groupname, r->artno);
			    if (!repair_spool)
				delete_article(r);
			    else
				relink = 1;
			}
		    } else {
			if (st2.st_ino != st.st_ino)
			    relink = 1;
		    }
		    if (relink) {
			/* atomically regenerate link to make sure the
			 * article is not lost -- unlink+link is not
			 * safe */
			(void)unlink(".to.relink");
			if (link(name, ".to.relink")
			    || rename(".to.relink", m)) {
			    ln_log(LNLOG_SERR,
				   LNLOG_CARTICLE,
				   "%s: cannot restore hard link "
				   "%s -> %s: %m", groupname, name, m);
			} else {
			    ln_log(LNLOG_SINFO,
				   LNLOG_CARTICLE,
				   "%s: restored hard link "
				   "%s -> %s", groupname, name, m);
			}
		    }
		}
	    }
	}
    }

    if (debugmode & DEBUG_EXPIRE)
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP, "%s: exit updatedir", groupname);
}

/*
 * find lowest article number, lower than high,
 * also count total number of articles
 */
unsigned long
low_wm(unsigned long high)
{
    unsigned long low, i;
    struct rnode *r;

    low = high;
    kept = 0;
    for (i = 0; i < HASHSIZE; ++i) {
	r = hashtab[i];
	while (r) {
	    if (r->artno) {	/* don't count nonexisting articles */
		++kept;
		if (r->artno < low) {
		    low = r->artno;
		}
	    }
	    r = r->nhash;
	}
    }
    return low;
}

static time_t
lookup_expire(char *group)
{
    struct expire_entry *a;

    for (a = expire_base; a; a = a->next) {
	if (ngmatch(a->group, group) == 0)
	    return a->xtime;
    }
    return default_expire;
}

/*
 * return 1 if xover is a legal overview line, 0 else
 */
static int
legalxoverline(char *xover, unsigned long artno)
{
    char *p;
    char *q;

    if (!xover)
	return 0;

    /* anything that isn't tab, printable ascii, or latin-* ? then killit */

    p = xover;
    while (*p) {
	int c = (unsigned char)*p++;

	if ((c != '\t' && c < ' ') || (c > 126 && c < 160)) {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: non-printable chars.", artno);
	    return 0;
	}
    }

    p = xover;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no Subject: header.", artno);
	return 0;
    }

    /* article number */

    while (p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: article "
		       "number must consists of digits.", artno);
	    return 0;
	}
	p++;
    }

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no From: header.", artno);
	return 0;
    }

    /* subject: no limitations */

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no Date: header.", artno);
	return 0;
    }

    /* from: no limitations */

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no Message-ID: header.", artno);
	return 0;
    }

    /* date: no limitations */

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no References: or Bytes: header.", artno);
	return 0;
    }

    /* message-id: <*@*> */

    if (*p != '<') {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: Message-ID does not start with <.", artno);
	return 0;
    }
    while (p != q && *p != '@' && *p != '>' && *p != ' ')
	p++;
    if (*p != '@') {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: Message-ID does not contain @.", artno);
	return 0;
    }
    while (p != q && *p != '>' && *p != ' ')
	p++;
    if ((*p != '>') || (++p != q)) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: Message-ID does not end with >.", artno);
	return 0;
    }

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no Bytes: header.", artno);
	return 0;
    }

    /* references: a series of <*@*> separated by space */

    while (p != q) {
	if (*p != '<') {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: "
		       "Reference does not start with <.", artno);
	    return 0;
	}
	while (p != q && *p != '@' && *p != '>' && *p != ' ')
	    p++;
	if (*p != '@') {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: Reference does not contain @.", artno);
	    return 0;
	}
	while (p != q && *p != '>' && *p != ' ')
	    p++;
	if (*p++ != '>') {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: Reference does not end with >.",
		       artno);
	    return 0;
	}
	while (p != q && *p == ' ')
	    p++;
    }

    p = q + 1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode & DEBUG_EXPIRE)
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		   "%lu xover error: no Lines: header.", artno);
	return 0;
    }

    /* byte count */

    while (p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: illegal digit "
		       "in Bytes: header.", artno);
	    return 0;
	}
	p++;
    }

    p = q + 1;
    q = strchr(p, '\t');
    if (q)
	*q = '\0';		/* kill any extra fields */

    /* line count */

    while (p && *p && p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode & DEBUG_EXPIRE)
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "%lu xover error: illegal digit "
		       "in Lines: header.", artno);
	    return 0;
	}
	p++;
    }

    return 1;
}

/*
 * dogroup: expire group
 */
static void
dogroup(struct newsgroup *g, time_t expire)
{

    /* FIXME: why is getxover run twice? why is chdirgroup run
       * twice? */
    unsigned long first, last, i, totalthreads;
    struct thread *threadlist;

    deleted = kept = 0;

    /* skip empty groups */
    if (!chdirgroup(g->name, FALSE))
	return;

    /* barf on getcwd problems */
    if (!getcwd(gdir, PATH_MAX)) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
		"getcwd(...,%d) returned error: %m", PATH_MAX);
	return;
    }

    /* read overview information */
    freexover();
    if (!getxover(0))
	return;

    /* find low-water and high-water marks */
    first = ULONG_MAX;
    last = 0;
    for (i = 0; xoverinfo[i].artno && i < xcount; ++i) {
	if (xoverinfo[i].exists) {
	    if (first > xoverinfo[i].artno) {
		first = xoverinfo[i].artno;
	    }
	    if (last < xoverinfo[i].artno) {
		last = xoverinfo[i].artno;
	    }
	}
    }
    if (debugmode & DEBUG_EXPIRE)
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	       "%s: expire %lu, low water mark %lu, high water mark %lu",
	       g->name, expire, first, last);
    if (expire <= 0) {
	return;
    }
    /* check the syntax of the .overview info */
    if (debugmode & DEBUG_EXPIRE) {
	for (i = 0; i < xcount; ++i) {
	    if (xoverinfo[i].artno
		&& !legalxoverline(xoverinfo[i].text, xoverinfo[i].artno)) {
/*    xoverinfo[i].text = NULL; *//* I want these */
	    }
	}
    }
    threadlist = build_threadlist(xcount);
    totalthreads = count_threads(threadlist);
    updatedir(g->name);
    remove_newer(threadlist, expire);
    if (debugmode & DEBUG_EXPIRE) {
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	       "%s: threads total: %lu, to delete: %lu", g->name,
	       totalthreads, count_threads(threadlist));
    }
    delete_threads(threadlist);
    /* compute new low-water mark, count remaining articles */
    kept = 0;
    first = low_wm(last);
    /* free unused memory */
    free_threadlist(threadlist);
    threadlist = 0;

    g->first = first;
    if (last > g->last) {	/* try to correct insane newsgroup info */
	g->last = last;
    }
    if (dryrun)
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: running without dry-run "
	       "will delete %lu and keep %lu articles", g->name,
	       deleted, kept - deleted);
    else
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: %lu articles deleted, " "%lu kept", g->name, deleted, kept);
    if (!kept) {
	if (unlink(".overview") < 0)
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "unlink %s/.overview: %m", gdir);
	if (!chdir("..") && (isinteresting(g->name) == 0)) {
	    /* delete directory and empty parent directories */
	    while (rmdir(gdir) == 0) {
		if (!getcwd(gdir, PATH_MAX)) {
		    ln_log(LNLOG_SERR, LNLOG_CGROUP,
			   "getcwd(...,%d) returned error: %m", PATH_MAX);
		    return;
		}
		chdir("..");
	    }
	}
    }
    /* Once we're done and there's something left we have to update the
     * .overview file. Otherwise unsubscribed groups will never be
     * deleted.
     */
    if (chdirgroup(g->name, FALSE)) {
	getxover(1);
	freexover();
    }
}

static void
expiregroup(struct newsgroup *g)
{
    struct newsgroup *ng;
    time_t expire;

    ng = g;
    while (ng && ng->name) {
	expire = lookup_expire(ng->name);
	dogroup(ng, expire);
	ng++;
    }
}


static void
expiremsgid(void)
{
    int n;
    char **dl, **di;
    struct stat st;
    char s[PATH_MAX];

    deleted = kept = 0;

    for (n = 0; n < 1000; n++) {
	size_t slen;

	snprintf(s, sizeof(s), "%s/message.id/%03d", spooldir, n);
	slen = strlen(s);	/* not all snprintf implementations have
				 * reliable return values */
	if (chdir(s)) {
	    if (errno == ENOENT) {
		ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		       "creating missing directory %s", s);
		mkdir(s, MKDIR_MODE);
	    }
	    if (chdir(s)) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "chdir %s: %m", s);
		continue;
	    }
	}

	dl = dirlist(s, DIRLIST_NONDOT, NULL);
	if (!dl) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open directory %s: %m", s);
	    continue;
	}

	for (di = dl; *di; di++) {
	    const char *m;
	    if (*di[0] == '.')
		continue;
	    /* FIXME: do not stat more than once */

	    /* First, make sure that all wrongly-hashed
	       articles are deleted. */
	    m = lookup(*di);
	    if (strncmp(m, s, slen) && !dryrun) {
		if (0 == log_unlink(*di)) {
		    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
			   "%s/%s was bogus, removed", s, *di);
		    deleted++;
		}
	    } else if (stat(*di, &st) == 0) {
		/* Then, check if the article has expired. */
		if (st.st_nlink < 2 && !dryrun && !unlink(*di)) {
		    ln_log(LNLOG_SDEBUG,
			   LNLOG_CARTICLE,
			   "%s/%s has less than 2 links, deleting", s, *di);
		    deleted++;
		} else {
		    if (S_ISREG(st.st_mode)) {
			kept++;
		    }
		}
	    }
	}
	free_dirlist(dl);
    }

    ln_log(LNLOG_SINFO, LNLOG_CTOP,
	   "message.id: %lu articles deleted, %lu kept", deleted, kept);
}

static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "texpire -V\n"
	    "    print version on stderr and exit\n"
	    "texpire [-fvnr] [-D debugmode] [-F configfile]\n"
	    "    -D: switch on debugmode\n"
	    "    -n: dry run mode, do not delete anything\n"
	    "    -r: relink articles with message.id tree\n"
	    "    -f: force expire irrespective of access time\n"
	    "    -v: more verbose (may be repeated)\n"
	    "    -F: use \"configfile\" instead of %s/config\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n",
	    libdir);
}

int
main(int argc, char **argv)
{
    time_t now;
    int option, reply;
    char conffile[4096];

    snprintf(conffile, sizeof(conffile), "%s/config", libdir);

    ln_log_open("texpire");

    if (!initvars(argv[0]))
	exit(EXIT_FAILURE);

    while ((option = getopt(argc, argv, "F:VD:vfrn")) != -1) {
	if (parseopt("texpire", option, optarg, conffile, sizeof(conffile))) {
	    /* FIXME: what happens here? */
	    ;
	} else
	    switch (option) {
	    case 'f':
		use_atime = 0;
		break;
	    case 'r':
		repair_spool = 1;
		break;
	    case 'n':
		dryrun = 1;
		break;
	    case 'F':
		break;
	    default:
		usage();
		exit(EXIT_FAILURE);
	    }
    }
    debug = debugmode;
    expire_base = NULL;
    if ((reply = readconfig(conffile)) != 0) {
	fprintf(stderr,
		"Reading configuration from %s failed (%s).\n",
		conffile, strerror(reply));
	exit(2);
    }

    whoami();
    if (lockfile_exists(FALSE))
	exit(EXIT_FAILURE);
    rereadactive();
    if (!active) {
	fprintf(stderr, "Reading active file failed, exiting "
		"(see syslog for more information).\n"
		"Has fetchnews been run?\n");
	exit(2);
    }

    if (verbose) {
	printf("texpire %s: ", version);
	if (use_atime)
	    printf("check mtime and atime\n");
	else
	    printf("check mtime only\n");
    }

    if (verbose) {
	printf("Checking if there are local posts sitting in the queue...\n");
    }
    feedincoming();

    if (debugmode & DEBUG_EXPIRE) {
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	       "texpire %s: use_atime is %d; repair_spool is %d",
	       version, use_atime, repair_spool);
    }
    if (default_expire == 0) {
	fprintf(stderr, "%s: no expire time\n", argv[0]);
	exit(2);
    }

    now = time(NULL);

    expiregroup(active);
    writeactive();
    freeactive();		/* throw away active data */
    freexover();		/* throw away overview data */
    expiremsgid();
    /* do not release the lock earlier to prevent confusion of other daemons */
    (void)log_unlink(lockfile);
    freeconfig();
    return 0;
}
