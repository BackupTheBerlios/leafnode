/*
  fetchnews -- post articles to and get news from upstream server(s)
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/
#include "leafnode.h"
#include "get.h"
#include "critmem.h"
#include "ln_log.h"
#include "mastring.h"
#include "format.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <utime.h>

#ifdef DEBUG_DMALLOC
#include <dmalloc.h>
#endif

int debug = 0;
unsigned long globalfetched = 0;
unsigned long globalkilled = 0;
unsigned long groupfetched;
unsigned long groupkilled;
struct serverlist *current_server;
time_t now;

/* Variables set by command-line options which are specific for fetch */
unsigned long extraarticles = 0;
unsigned int throttling = 0;
/* the higher the value, the less bandwidth is used */
int postonly = 0;		/* if 1, don't read files from upstream server */
int noexpire = 0;		/* if 1, don't automatically unsubscribe newsgroups */
int forceactive = 0;		/* if 1, reread complete active file */
int headerbody = 0;		/* if 1, get headers only; if 2, get bodies only */
static jmp_buf jmpbuffer;
static int isgrouponserver(char *newsgroups);
static int ismsgidonserver(char *msgid);
unsigned long getgroup(struct newsgroup *g, unsigned long server);
int postarticles(void);
unsigned long getarticle(struct filterlist *);

static void
_ignore_answer(FILE * f)
{
    char *l;

    while (((l = getaline(f)))) {
	if (!strcmp(l, "."))
	    break;
    }
}

/**
 * generate name for upstream server info file.
 * \return 
 */
static char *
server_info(const char *spool, const char *server, 
	    const unsigned short port, const char *suffix)
{
    mastr *s = mastr_new(0);
    char *res;
    char portstr[20] = "";

    if (port) {
	portstr[0] = ':';
	str_ulong(portstr + 1, port);
    }

    mastr_vcat(s, spool, "/leaf.node/", server, port, suffix, 0);

    res = critstrdup(mastr_str(s), "server_info");
    mastr_delete(s);
    return res;
}

static void
sigcatch(int signo)
{
    if (signo == SIGINT || signo == SIGTERM || signo == SIGALRM)
	longjmp(jmpbuffer, 1);
    else if (signo == SIGUSR1)
	verbose++;
    else if (signo == SIGUSR2)
	verbose--;
}

static struct serverlist *
findserver(char *servername)
{
    struct serverlist *sl;

    sl = servers;
    while (sl) {
	if (sl->name && strcasecmp(servername, sl->name) == 0) {
	    return sl;
	}
	sl = sl->next;
    }
    return NULL;
}

static int
testheaderbody(int optchar)
{
    if (!delaybody) {
	printf
	    ("Option -%c can only be used in conjunction with delaybody -"
	     "ignored\n", optchar);
	return 0;
    }
    if (headerbody == 1) {	/* already seen -H */
	printf("Option -%c and -H cannot be used together - ignored\n",
	       optchar);
	return 0;
    }
    if (headerbody == 2) {	/* already seen -B */
	printf("Option -%c and -B cannot be used together - ignored\n",
	       optchar);
	return 0;
    }
    return 1;
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n"
	    "fetchnews -V\n"
	    "    print version on stderr and exit\n"
	    "fetchnews [-Dv] [-F configfile] [-S server] -M message-id\n"
	    "fetchnews [-BDHnv] [-x #] [-F configfile] [-S server] [-t #] -N newsgroup\n"
	    "fetchnews [-BDfHnPv] [-x #] [-F configfile] [-S server] [-t #]\n"
	    "    -B: get article bodies only(works only with \"delaybody\" set)\n"
	    "    -D: switch on debugmode\n"
	    "    -f: force reload of groupinfo file\n"
	    "    -F: use \"configfile\" instead of %s/config\n"
	    "    -H: get article headers only(works only with \"delaybody\" set)\n"
	    "    -n: switch off automatic unsubscription of groups\n"
	    "    -P: post only, don't get new articles\n"
	    "    -v: verbose mode(may be repeated)\n"
	    "    -N newsgroup: get only articles in \"newsgroup\"\n"
	    "    -S server: only get articles from \"server\"\n"
	    "    -t delay: wait \"delay\" seconds between articles\n"
	    "See also the leafnode homepage at\n"
	    "    http://www.leafnode.org/\n", libdir);
}

/*
 * check whether any of the newsgroups is on server
 * return TRUE if yes, FALSE otherwise
 */
static int
isgrouponserver(char *newsgroups)
{
    char *p, *q;
    int retval;

    if (!newsgroups)
	return FALSE;

    retval = FALSE;
    p = newsgroups;
    do {
	q = strchr(p, ',');
	if (q)
	    *q++ = '\0';
	putaline(nntpout, "GROUP %s", p);
	if (nntpreply() == 211)
	    retval = TRUE;
	p = q;
	while (p && *p && isspace((unsigned char)*p))
	    p++;
    } while (q && !retval);
    return retval;
}

/*
 * check whether message-id is on server
 * return TRUE if yes, FALSE otherwise
 *
 * Since the STAT implementation is buggy in some news servers
 * (NewsCache), we use HEAD instead, although this causes more traffic.
 */
static int
ismsgidonserver(char *msgid)
{
    char *l;
    long a;

    if (!msgid)
	return FALSE;

    if (!stat_is_evil) {
	putaline(nntpout, "STAT %s", msgid);
	l = getaline(nntpin);
	if (get_long(l, &a) == 1) {
	    switch (a) {
	    case 223:
		return TRUE;
	    case 430:
		return FALSE;
	    default:		/* the server is undecisive, 
				   fall back to HEAD */
		/* FIXME: should log protocol error and pass error
		   to upper layers */
		break;
	    }
	}
    }

    putaline(nntpout, "HEAD %s", msgid);
    l = getaline(nntpin);
    if (get_long(l, &a) == 1 && a == 221) {
	_ignore_answer(nntpin);
	return TRUE;
    } else {
	return FALSE;
    }
}

/*
 * get articles by using NEWNEWS
 * returns last number of processed article or 0 if NEWNEWS didn't work
 *
 * unfortunately DNews returns 230 even if NEWNEWS is disabled
 */
static unsigned long
donewnews(struct newsgroup *g, time_t lastrun)
{
    char timestr[64];

    ln_log(LNLOG_SERR, LNLOG_CTOP,
	   "WARNING: donewnews called, not yet implemented, aborting");
    abort();
    /* these two lines borrowed from nntpactive() */
#ifdef NOTYET
    strftime(timestr, 64, "%Y%m%d %H%M00", gmtime(&lastrun));
    ln_log(LNLOG_SINFO, LNLOG_CGROUP, "doing NEWNEWS %s %s",
	   g->name, timestr + 2);
    putaline(nntpout, "NEWNEWS %s %s", g->name, timestr + 2);
    if (nntpreply() != 230) {
	return 0;		/* NEWNEWS not supported or something going wrong */
    }
/* #ifdef NOTYET */
    while ((l = getaline(nntpin)) && strcmp(l, ".")) {
	/* get a list of message ids: put them into a stringlist and
	   request them one by one */
    }
    return 1;
#else
    _ignore_answer(nntpin);
    return 0;
#endif
}

/*
 * get an article by message id
 */
static int
getbymsgid(char *msgid)
{
    putaline(nntpout, "ARTICLE %s", msgid);
    return (getarticle(NULL) > 0) ? TRUE : FALSE;
}

/*
 * Get bodies of messages that have marked for download.
 * The group must already be selected at the remote server and
 * the current directory must be the one of the group.
 */
static void
getmarked(struct newsgroup *group)
{
    FILE *f;
    char *l;
    struct stringlist *failed = NULL;
    struct stringlist *ptr = NULL;
    char filename[1024];

    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "Getting bodies of marked messages for group %s ...\n", group->name);
    sprintf(filename, "%s/interesting.groups/%s", spooldir, group->name);
    if (!(f = fopen(filename, "r"))) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
	       "Cannot open %s for reading: %m", filename);
	return;
    }
    while ((l = getaline(f))) {
	putaline(nntpout, "ARTICLE %s", l);
	if (!getarticle(NULL))
	    appendtolist(&failed, &ptr, l);
    }
    fclose(f);
    truncate(filename, (off_t) 0);	/* kill file contents */
    if (!failed)
	return;
    /* write back ids of all articles which could not be retrieved */
    if (!(f = fopen(filename, "w")))
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Cannot open %s for writing: %m", filename);
    else {
	ptr = failed;
	while (ptr) {
	    fputs(ptr->string, f);
	    fputc('\n', f);
	    ptr = ptr->next;
	}
	fclose(f);
    }
    freelist(failed);
}

/*
 * get newsgroup from a server. "server" is the last article that
 * was previously read from this group on that server
 */
/*
 * fopenmsgid: open a message id file, repairing errors if possible
 */
static FILE *
fopenmsgid(const char *filename)
{
    FILE *f;
    char *c, *slp;
    struct stat st;

    if (!stat(filename, &st)) {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
	   "article %s already stored", filename);
	return NULL;
    } else if (errno == ENOENT) {
	if ((f = fopen(filename, "w")) == NULL) {
	    /* check whether directory exists and possibly create new one */
	    c = critstrdup(filename, "main");
	    slp = strrchr(c, '/');
	    if (slp)
		*slp = '\0';
	    if (stat(c, &st)) {
		if (mkdir(c, 0775) < 0) {
		    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
			   "Cannot create directory %s: %m", c);
		}
	    }
	    free(c);
	    f = fopen(filename, "w");	/* Try opening file again */
	}
	return f;
    } else {
	ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
	       "unable to store article %s: %m", filename);
	return NULL;
    }
}

/*
 * calculate first and last article number to get
 * returns 0 for error, 1 for success, -1 if group is not available at all
 */
static int
getfirstlast(struct newsgroup *g, unsigned long *first, unsigned long *last)
{
    unsigned long h, window;
    long n;
    char *l;

    putaline(nntpout, "GROUP %s", g->name);
    l = getaline(nntpin);
    if (!l)
	return 0;

    if (get_long(l, &n) && n == 480) {
	return -1;
    }

    if (get_long(l, &n) && n == 411) {
	/* group not available on server */
	return -1;
    }

    if (sscanf(l, "%3ld %lu %lu %lu ", &n, &h, &window, last) < 4 || n != 211)
	return 0;

    if (*last == 0) {		/* group available but no articles on server */
	*first = 0;
	return 0;
    }
    if (extraarticles) {
	if (*first > extraarticles)
	    h = *first - extraarticles;
	else
	    h = 0;
	if (h < window)
	    h = window;
	else if (h < 1)
	    h = 1;
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: backing up from %lu to %lu\n", g->name, h, *first);
	*first = h;
    }

    if (*first > *last + 1) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: last seen article was %lu, server now has %lu - %lu",
	       g->name, *first, window, *last);
	if (*first > (*last + 5)) {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: switched upstream servers? %lu > %lu\n",
		   g->name, *first, *last);
	    *first = window;	/* check all upstream articles again */
	} else {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: rampant spam cancel? %lu > %lu\n",
		   g->name, *first - 1, *last);
	    *first = *last - 5;	/* re-check last five upstream articles */
	}
    }
    if (initiallimit && (*first == 1) && (*last - *first > initiallimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: skipping articles %lu-%lu inclusive (initial limit)",
	       g->name, *first, *last - initiallimit);
	*first = *last - initiallimit + 1;
    }
    if (artlimit && (*last + 1 - *first > artlimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: skipping articles %lu-%lu inclusive (article limit)",
	       g->name, *first, *last - artlimit);
	*first = *last + 1 - artlimit;
    }
    if (window < *first)
	window = *first;
    if (window < 1)
	window = 1;
    *first = window;
    if (*first > *last) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP, "%s: no new articles", g->name);
	return 0;
    }
    return 1;
}

/*
 * get headers of articles with XOVER and return a stringlist of article
 * numbers to get
 * return -1 for error
 * return -2 if XOVER was rejected
 */
static int
doxover(struct stringlist **stufftoget,
	unsigned long first, unsigned long last,
	struct filterlist *filtlst, char *groupname)
{
    char *l;
    unsigned long count = 0;
    char *c = 0;
    long reply;
    struct stringlist *helpptr = NULL;

    putaline(nntpout, "XOVER %lu-%lu", first, last);
    l = getaline(nntpin);
    if ((!get_long(l, &reply)) || (reply != 224)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XOVER command: %s", l);
	return -2;
    }
    while ((l = getaline(nntpin)) && strcmp(l, ".")) {
	int xoverlen = strlen(l);
	char *xover[20];
	char *artno, *subject, *from, *date, *messageid;
	char *references, *lines, *bytes, *p, *q;
	char *newsgroups = NULL;
	int freenewsgroups = 0;

	if (abs(strnsplit(xover, l, "\t", sizeof(xover) / sizeof(xover[0]))) <
	    8) {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP,
		   "read broken xover line, too few fields: \"%s\", skipping",
		   l);
	    free_strlist(xover);
	    continue;
	}

	/* format of an XOVER line is usually:
	   article number, subject, author, date, message-id, references,
	   byte count, line count, xhdr (optional) */
	artno = xover[0];
	subject = xover[1];
	from = xover[2];
	date = xover[3];
	messageid = xover[4];
	references = xover[5];
	bytes = xover[6];
	lines = xover[7];

	/* is there an Xref: header present as well? */
	p = xover[8];
	if (p == 0 || *p == '\0') {
	    /* no Xref: header field */
	    newsgroups = groupname;
	} else {
	    /* have Xref: header field, check if it contains Xref: */
	    if (strisprefix(p, "Xref:")) {
		/* something here, but it's no Xref: header */
		newsgroups = groupname;
	    }
	}

	if (!newsgroups) {
	    /* Xref: header is present and must be parsed */
	    p += 5;
	    SKIPLWS(p);
	    if (*p == '\0') {
		/* Xref: header but no newsgroups given */
		newsgroups = groupname;
	    } else {
		/* Allocating space for pseudo-newsgroups header */
		newsgroups = (char *)critcalloc(strlen(p),
						"Allocating space for "
						"newsgroups header");
		freenewsgroups = 1;

		/* skip server name */
		SKIPWORD(p);

		/* now the thing points to the first newsgroup, if present */
		while (p && *p && (q = strchr(p, ':')) != NULL) {
		    *q = '\0';
		    strcat(newsgroups, p);
		    p = q + 1;
		    SKIPWORD(p);	/* skip article number and subsequent
					   white space */
		    if (*p)
			/* more than one newsgroup */
			strcat(newsgroups, ",");
		}
	    }
	}

	if ((filtermode & FM_XOVER) || delaybody) {
	    char *hdr;

	    hdr = (char *)critmalloc(xoverlen + strlen(newsgroups) + 200,
				     "allocating space for pseudo header");
	    sprintf(hdr, "From: %s\n"
		    "Subject: %s\n"
		    "Message-ID: %s\n"
		    "References: %s\n"
		    "Date: %s\n"
		    "Newsgroups: %s\n",
		    from, subject, messageid, references, date, newsgroups);

	    if (lines) {
		strcat(hdr, "Lines: ");
		strcat(hdr, lines);
		strcat(hdr, "\n");
	    }
	    if (bytes) {
		strcat(hdr, "Bytes: ");
		strcat(hdr, bytes);
		strcat(hdr, "\n");
	    }
	    if (filtlst && killfilter(filtlst, hdr)) {
		groupkilled++;
		/* filter pseudoheaders */
		free(hdr);
		if (freenewsgroups)
		    free(newsgroups);
		free_strlist(xover);
		continue;
	    }
	    if (ihave(messageid)) {
		/* we have the article already */
		if (freenewsgroups)
		    free(newsgroups);
		free(hdr);
		free_strlist(xover);
		continue;
	    }

	    if (delaybody) {
		/* write pseudoarticle */
		FILE *f;

		if (!c) {
		    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
			   "lookup of %s failed", messageid);
		    free(hdr);
		    free_strlist(xover);
		    continue;
		}
		if ((f = fopenmsgid(c)) != NULL) {
		    fprintf(f, "%s", hdr);
		    fclose(f);
		    store(c, 0, 0);	/* FIXME: filter here! */
		}
	    } else {
		count++;
		appendtolist(stufftoget, &helpptr, artno);
	    }
	    free(hdr);
	} else {
	    count++;
	    appendtolist(stufftoget, &helpptr, artno);
	}
	if (freenewsgroups) {
	    free(newsgroups);
	}
	free_strlist(xover);
    }
    if (strcmp(l, ".") == 0)
	return count;
    else
	return -1;
}

/*
 * use XHDR to check which articles to get. This is faster than XOVER
 * since only message-IDs are transmitted, but you lose some features
 * return -1 for error
 * return -2 if XHDR was rejected
 */
static int
doxhdr(struct stringlist **stufftoget, unsigned long first, unsigned long last)
{
    char *l;
    unsigned long count = 0;
    long reply;
    struct stringlist *helpptr = NULL;

    putaline(nntpout, "XHDR message-id %lu-%lu", first, last);
    l = getaline(nntpin);
    if ((!get_long(l, &reply)) || (reply != 221)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XHDR command: %s", l);
	return -1;
    }
    debug = 0;
    while ((l = getaline(nntpin)) && strcmp(l, ".")) {
	/* format is: [# of article] [message-id] */
	char *t;

	t = l;
	SKIPWORD(t);
	if (ihave(t))
	    continue;
	/* mark this article */
	count++;
	appendtolist(stufftoget, &helpptr, l);
    }
    debug = debugmode;
    if (strcmp(l, ".") == 0)
	return count;
    else
	return -1;
}

/*
 * get articles
 */

/*
 * Get a single article (incl. filtering), return article number or 0.
 * \return 0 == error receiving article 
 */
unsigned long
getarticle(struct filterlist *filtlst)
{
    char *l;
    int reply = 0;
    unsigned long artno;

    l = getaline(nntpin);
    if ((sscanf(l, "%3d %lu", &reply, &artno) != 2) || (reply / 10 != 22)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "Wrong reply to ARTICLE command: %s", l);
	return 0;
    }

    switch (store_stream(nntpin, 1, (filtermode & FM_HEAD ? filtlst : NULL),
			 -1)) {
    case 1:
	groupkilled++;
	return artno;
    case 0:
	groupfetched++;
	return artno;
    default:
	return 0;
    }
}

static const char *
chopmid(const char *in)
{
    /* chops off second word */
    static char buf[1024];
    char *j = buf, *k;
    mastrncpy(buf, in, sizeof(buf));
    SKIPLWS(j);
    k = j;
    while (isdigit((unsigned char)*k))
	k++;
    *k = '\0';			/* chop off message-ID in case we used XHDR */
    return buf;
}

/*
 * get all articles in a group, with pipelining NNTP commands
 */
static unsigned long
getarticles(struct stringlist *stufftoget, long n, struct filterlist *f)
{
    struct stringlist *p;
    long advance = 0, window;
    unsigned long server = 0;
    long remain = sendbuf;

    p = stufftoget;
    window = (n < windowsize) ? n : windowsize;
    while (p || advance) {
	remain = sendbuf;
	while (p && advance < window && (advance == 0 || remain > 100)) {
	    const char *c;
	    fprintf(nntpout, "ARTICLE %s\r\n", c = chopmid(p->string));
	    remain -= 10 + strlen(c);
	    p = p->next;
	    advance++;
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE, "sent ARTICLE %s command, "
		   "advance: %ld", c, advance);
	    if (throttling)
		sleep(throttling);
	}
	fflush(nntpout);
	while (advance) {
	    server = getarticle(f);
	    /* FIXME: add timeout here and force window to 1 if it triggers */
	    if (p) {
		const char *c;
		fprintf(nntpout, "ARTICLE %s\r\n", c = chopmid(p->string));
		fflush(nntpout);
		p = p->next;
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "received article, sent ARTICLE %s command, advance: %ld",
		       c, advance);
		if (throttling)
		    sleep(throttling);
	    } else {
		advance--;
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "received article, advance: %ld", advance);
	    }
	}
    }
    return server;
}

/*
 * getgroup():
 * returns 0 if group is unavailable, otherwise last article number in that
 * group
 */
unsigned long
getgroup(struct newsgroup *g, unsigned long first)
{
    struct stringlist *stufftoget = NULL;
    struct filterlist *f = NULL;
    long x = 0;
    long outstanding = 0;
    unsigned long last = 0;

    /* lots of plausibility tests */
    if (!g)
	return first;
    if (!isinteresting(g->name))
	return 0;
    groupfetched = 0;
    groupkilled = 0;
    /* get marked articles first */
    if (delaybody && (headerbody != 1)) {
	getmarked(g);
	if (headerbody == 2) {
	    /* get only marked bodies, nothing else */
	    return first;
	}
    }

    if (!first)
	first = 1;

    if (g->first > g->last)
	g->last = g->first;

    (void)chdirgroup(g->name, TRUE);	/* to create the directory */

    x = getfirstlast(g, &first, &last);

    switch (x) {
    case 0:
	return first;
    case -1:
	return 0;
    default:
	break;
    }


    if (filter)
	f = selectfilter(g->name);

    /* use XOVER since it is often faster than XHDR. User may prefer
       XHDR if they know what they are doing and no additional information
       is requested */
    if (current_server->usexhdr && !f && !delaybody) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %ld articles %lu - %lu, using XHDR", g->name,
	       (last - first + 1), first, last);
	outstanding = doxhdr(&stufftoget, first, last);
    } else {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %ld articles %lu - %lu, using XOVER", g->name,
	       (last - first + 1), first, last);
	outstanding = doxover(&stufftoget, first, last, f, g->name);
    }

    switch (outstanding) {
    case -2:
	current_server->usexhdr = 0;	/* disable XOVER */
	/* fall through to -1 */
    case -1:
	freefilter(f);
	freelist(stufftoget);
	return first;		/* error; consider again */
    case 0:
	freefilter(f);
	freelist(stufftoget);
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: all articles already there", g->name);
	return (last + 1);	/* all articles already here */
    default:
	break;
    }

    if (delaybody) {
	freefilter(f);
	freelist(stufftoget);
	return last;
    }

    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "%s: will fetch %ld articles", g->name, outstanding);

    getarticles(stufftoget, outstanding, f);

    freefilter(f);
    freelist(stufftoget);

    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
	   "%s: %lu articles fetched (to %lu), %lu killed",
	   g->name, groupfetched, g->last, groupkilled);
    globalfetched += groupfetched;
    globalkilled += groupkilled;
    return (last + 1);
}

/*
 * get active file from current_server
 */
static void
nntpactive(void)
{
    struct stat st;
    char *l, *p, *q;
    struct stringlist *groups = NULL;
    struct stringlist *helpptr = NULL;
    char s[PATH_MAX];		/* FIXME: possible overrun below */
    char timestr[64];		/* must store at least a date in YYMMDD HHMMSS format */
    int reply = 0, error;
     DIR *ng;
     struct dirent * nga;
     unsigned long last, first, i;
    unsigned long count = 0;

    sprintf(s, "%s/leaf.node/last:%s:%d", spooldir, current_server->name,
	    current_server->port);

    if (forceactive) {
	int r = unlink(s);
	if (r && errno != ENOENT) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot unlink %s: %m", s);
	}
    }
    if ((0 == stat(s, &st))) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "%s: checking for new newsgroups", current_server->name);
	/* "%Y" and "timestr + 2" avoid Y2k compiler warnings */
	strftime(timestr, 64, "%Y%m%d %H%M%S", gmtime(&st.st_mtime));
	putaline(nntpout, "NEWGROUPS %s GMT", timestr + 2);
	if (nntpreply() != 231) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER, "Reading new newsgroups failed");
	    return;
	}
	while ((l = getaline(nntpin)) && (strcmp(l, ".") != 0)) {
	    count ++;
	    p = l;
	    while (!isspace((unsigned char)*p))
		p++;
	    if (*p) {
		*p = '\0';
		p++;
		p += strlen(p) - 1; /* p contains status char */
	    }
	    insertgroup(l, *p, 0, 0, time(NULL), NULL);
	    appendtolist(&groups, &helpptr, l);
	}
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "%s: found %lu new newsgroups", current_server->name, count);
	if (!l)
	    /* timeout */
	    return;
	mergegroups();		/* merge groups into active */
	helpptr = groups;
	if (count) {
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		   "%s: getting new newsgroup descriptions",
		   current_server->name);
	}
	while (count && helpptr && current_server->descriptions) {
	    error = 0;
	    putaline(nntpout, "LIST NEWSGROUPS %s", helpptr->string);
	    reply = nntpreply();
	    if (reply == 215) {
		l = getaline(nntpin);
		if (l && *l != '.') {
		    p = l;
		    CUTSKIPWORD(p);
		    changegroupdesc(l, *p ? p : NULL);
		    do {
			l = getaline(nntpin);
			error++;
		    } while (l && *l && *l != '.');
		    if (error > 1) {
			current_server->descriptions = 0;
			ln_log(LNLOG_SWARNING, LNLOG_CSERVER,
			       "%s: warning: server does not process "
			       "LIST NEWSGROUPS %s correctly: use nodesc",
			       current_server->name, helpptr->string);
		    }
		}
	    }
	    helpptr = helpptr->next;
	}
	freelist(groups);
    } else {
	last = first = 0;
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	       "%s: getting newsgroups list", current_server->name);
	putaline(nntpout, "LIST");
	if (nntpreply() != 215) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER, "%s: reading all newsgroups failed",
		   current_server->name);
	    return;
	}
	while ((l = getaline(nntpin)) && (strcmp(l, "."))) {
	    count ++;
	    p = l;
	    while (!isspace((unsigned char)*p) )
                p++;
	    while ( isspace((unsigned char)*p) ) {
                *p = '\0';
                p++;
                p += strlen(p) - 1; /* p contains the status char */
	    }
	    /* see if the newsgroup is interesting.  if it is, and we
	       don't have it in groupinfo, figure water marks */
	    /* FIXME: save high water mark in .last.posting? */
            if (isinteresting(l)
		&& !(active && findgroup(l))
                && chdirgroup(l, FALSE))
	    {
                first = ULONG_MAX;
                last = 0;
                ng = opendir(".");
                while ((nga = readdir(ng))) {
                    if (isdigit ((unsigned char)*(nga->d_name))) {
                        q = NULL;
                        i = strtoul(nga->d_name, &q, 10);
                        if (*q == '\0') {
                            if (i < first)
                                first = i;
                            if (i > last)
                                last = i;
                        }
                    }
                }
                if (first > last) {
                    first = 1;
                    last = 1;
                }
                closedir(ng);
            }
            insertgroup(l, p[0], first, last, 0, NULL);
	}
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	       "%s: read %lu newsgroups", current_server->name, count);

	mergegroups();
	if (!l)
	    /* timeout */
	    return;

	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	       "%s: getting newsgroup descriptions", current_server->name);
	putaline(nntpout, "LIST NEWSGROUPS");
	l = getaline(nntpin);
	/* correct reply starts with "215". However, INN 1.5.1 is broken
	   and immediately returns the list of groups */
	if (l) {
	    char *tmp;

	    reply = strtol(l, &tmp, 10);
	    if ((reply == 215) && (*tmp == ' ' || *tmp == '\0')) {
		l = getaline(nntpin);	/* get first description */
	    } else if (*tmp != ' ' && *tmp != '\0') {
		/* FIXME: INN 1.5.1: line already contains description */
		/* do nothing here */
	    } else {
		ln_log(LNLOG_SERR, LNLOG_CSERVER,
		       "%s: reading newsgroups descriptions failed: %s", 
		       current_server->name, l);
		return;
	    }
	} else {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER,
		   "%s: reading newsgroups descriptions failed",
		   current_server->name);
	    return;
	}
	while (l && (strcmp(l, "."))) {
	    p = l;
	    while (!isspace((unsigned char)*p))
		p++;
	    while (isspace((unsigned char)*p)) {
		*(p++) = '\0';
	    }
	    changegroupdesc(l, *p ? p : NULL);
	    l = getaline(nntpin);
	}
	if (!l)
	    return;		/* timeout */
    }
    /* touch file */
    {
	int fd;
	fd = open(s, O_CREAT | O_WRONLY | O_TRUNC, (mode_t) 0664);
	if (fd < 0 || close(fd) < 0)
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "cannot touch %s: %m", s);
    }
}

/* post article in open file f, return FALSE if problem, return TRUE if ok */
/* FIXME: add IHAVE */
static int
post_FILE(FILE * f, char **line)
{
    int r;
    char *l;

    rewind(f);
    putaline(nntpout, "POST");
    r = newnntpreply(line);
    if (r != 340)
	return 0;
    while ((l = getaline(f))) {
	/* can't use putaline() here because
	   line length is restricted to 1024 bytes in there */
	if (l[0] == '.')
	    fputc('.', nntpout);
	fputs(l, nntpout);
	fputs("\r\n", nntpout);
    };
    putaline(nntpout, ".");
    *line = getaline(nntpin);
    if (!*line)
	return FALSE;
    if (0 == strncmp(*line, "240", 3))
	return TRUE;
    return FALSE;
}

/** Strip off directories from file and move that file to dir below
 * spooldir, logging difficulties.
 * \return the return value of the embedded rename(2) syscall
 */
static
int moveto(const char *file /** source file */,
	   const char *dir /** destination directory */) {
    int r, e;

    mastr *s = mastr_new(PATH_MAX);
    mastr_vcat(s, spooldir, dir,
	       BASENAME(file), 0);
    r = rename(file, mastr_str(s));
    e = errno;
    if (r) ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot move %s to %s: %m",
		  file, dir);
    mastr_delete(s);
    errno = e;
    return r;
}

/*
 * post all spooled articles to currently connected server
 *
 * if all postings succeed, returns 1
 * if there are no postings to post, returns 1
 * if a posting is strange for some reason, returns 0
 */
int
postarticles(void)
{
    char *line = 0;
    FILE *f;
    int haveid, n;
    unsigned long articles;
    char **x, **y;

    x = spooldirlist_prefix("out.going", DIRLIST_NONDOT, &articles);
    if (!x) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read out.going: %m");
	return 0;
    }

    ln_log(LNLOG_SINFO, LNLOG_CSERVER, "found %ld articles in out.going.",
	   articles);

    n = 0;
    for (y = x; *y; y++) {
	haveid = 0;
	f = 0;
	if (!(f = fopen_reg(*y, "r"))) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "Cannot open %s to post, expecting regular file.", *y);
	} else {
	    char *f1;

	    f1 = fgetheader(f, "Newsgroups:", 1);
	    if (f1) {
		if (isgrouponserver(f1)) {
		    char *f2;

		    f2 = fgetheader(f, "Message-ID:", 1);
		    if (f2) {
			if (ismsgidonserver(f2)) {
			    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
				   "Message-ID of %s already in use upstream"
				   " -- discarding article", *y);
			    if (unlink(*y)) {
				ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				       "Cannot delete article %s: %m", *y);
				/* FIXME: don't fail here */
			    }
			} else {
			    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
				   "Posting %s", *y);
			    if (post_FILE(f, &line)) {
				/* POST was OK */
				if (checkstatus(f1, 'm'))
				    (void)moveto(*y, "/backup.moderated/");
				++n;
			    } else {
				/* POST failed */
				const char xx[] = "/failed.postings/";

				ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				       "Unable to post %s: \"%s\", "
				       "moving to %s/%s", *y, line,
				       spooldir, xx);
				(void)moveto(*y, xx);
			    }
			}
			free(f2);
		    }
		}
		free(f1);
	    }
	    log_fclose(f);
	}
    }
    free_dirlist(x);
    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	   "%s: %d articles posted", current_server->name, n);
    return 1;
}
static void
processupstream(const char *server, const int port, time_t lastrun)
{
    FILE *f;
    const char *ng;
    struct newsgroup *g;
    int havefile;
    int newnews_ok = FALSE;	/* FIXME */
    unsigned long newserver = 0;
    char *l;
    char *oldfile = 0;
    struct stringlist *ngs = NULL;
    struct stringlist *helpptr = NULL;
    char *s;
    RBLIST *r;

    /* read server info */
    s = server_info(spooldir, server, port, "");

    oldfile = critstrdup(s, "processupstream");
    havefile = 0;
    if ((f = fopen(s, "r"))) {
	/* FIXME: a sorted array or a tree would be better than a list */
	ngs = NULL;

	while ((l = getaline(f)) && *l) {
	    appendtolist(&ngs, &helpptr, l);
	}

	havefile = 1;
	fclose(f);
    } else {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", s);
    }

    critinitinteresting();

    r = openinteresting();
    if (!r) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open interesting.groups");
	free(s);
	return;
    }

    free(s);
    s = server_info(spooldir, server, port, "~");

    f = fopen(s, "w");
    if (f == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "Could not open %s for writing: %m", s);
    }
    while ((ng = readinteresting(r))) {
	if (isalnum((unsigned char)*ng)) {
	    g = findgroup(ng);
	    if (g != NULL) {
		sprintf(s, "%s ", g->name);
		l = havefile ? findinlist(ngs, s) : NULL;
		if (l && *l) {
		    char *t;
		    unsigned long a;

		    t = strchr(l, ' ');
		    if (t == NULL || *t == '\0')
			newserver = getgroup(g, 1);
		    else if (newnews_ok) {
			newserver = donewnews(g, lastrun);
			if (!newserver)
			    newnews_ok = FALSE;
		    }
		    if (!newnews_ok) {
			a = strtoul(t, NULL, 10);
			if (a)
			    newserver = getgroup(g, a);
			else
			    newserver = getgroup(g, 1);
		    }
		} else
		    newserver = getgroup(g, 1);
		if ((f != NULL) && newserver)
		    fprintf(f, "%s %lu\n", g->name, newserver);
	    } else { /* g != NULL */
		if (!forceactive || (debug & DEBUG_ACTIVE))
		    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
			   "%s not found in groupinfo file", ng);
	    }
	}
    }
    closeinteresting(r);
    free(s);

    if (f) {
	fclose(f);
	s = server_info(spooldir, server, port, "~");
	(void)log_rename(s, oldfile);
    }
    free(oldfile);
    freelist(ngs);
}


/*
 * get a command line parameter as an int
 */
static int
getparam(char *arg)
{
    char *c = NULL;
    int value;

    if (!arg) {
	usage();
	exit(EXIT_FAILURE);
    }
    value = strtol(arg, &c, 10);
    if (c && !*c)
	return (value);
    else {
	usage();
	exit(EXIT_FAILURE);
    }
}

/*
 * works current_server. Returns TRUE if no other servers have to be queried,
 * FALSE otherwise
 */
static int
do_server(char *msgid, time_t lastrun, char *newsgrp)
{
    int reply;

    fflush(stdout);
    reply = nntpconnect(current_server);
    if (reply) {
	if (current_server->username)
	    (void)authenticate();
	putaline(nntpout, "MODE READER");
	reply = nntpreply();
	if (reply < 0) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER,
		   "Problem connecting to %s", current_server->name);
	} else if (msgid) {
	    /* if retrieval of the message id is successful at one
	       server, we don't have to check the others */
	    if (getbymsgid(msgid)) {
		return TRUE;
	    }
	} else if (newsgrp) {
	    /* FIXME */
	} else {
	    if (reply == 200 && !current_server->dontpost)
		(void)postarticles();
	    if (!postonly) {
		nntpactive();
		/* get list of newsgroups or new newsgroups */
		processupstream(current_server->name, current_server->port,
				lastrun);
	    }
	}
	putaline(nntpout, "QUIT");	/* say it, then just exit :) */
	nntpdisconnect();
    }
    return FALSE;
}

/*
 * main program
 */
int
main(int argc, char **argv)
{
    int option, reply, flag;
    time_t starttime;
    volatile time_t lastrun;
    char conffile[PATH_MAX + 1];
    static char *msgid = NULL;
    static char *newsgrp = NULL;
    char *t;
    int did_not_fork, err;

    verbose = 0;
    postonly = 0;
    if (((err = snprintf(conffile, sizeof(conffile), "%s/config", libdir)) < 0)
	|| (err >= (int)sizeof(conffile)))
	exit(EXIT_FAILURE);
    if (!initvars(argv[0]))
	exit(EXIT_FAILURE);
    ln_log_open("fetchnews");
    starttime = time(NULL);
    now = time(NULL);
    umask(2);
    whoami();
    servers = NULL;
    t = getoptarg('F', argc, argv);
    if (t)
	strcpy(conffile, t);
    if ((reply = readconfig(conffile)) != 0) {
	printf("Reading configuration failed (%s).\n", strerror(reply));
	exit(2);
    }
    flag = FALSE;
    while ((option = getopt(argc, argv, "VDHBPF:S:N:M:fnvx:p:t:")) != -1) {
	if (parseopt("fetchnews", option, optarg, conffile, sizeof(conffile))) {
	    ;
	} else if (option == 't') {
	    throttling = getparam(optarg);
	} else if (option == 'x') {
	    extraarticles = getparam(optarg);
	} else if ((option == 'S') && optarg && strlen(optarg)) {
	    struct serverlist *sl;
	    char *p;

	    if (!flag) {
		/* deactive all servers but don't delete them */
		sl = servers;
		while (sl) {
		    sl->active = FALSE;
		    sl = sl->next;
		}
		flag = TRUE;
	    }
	    sl = findserver(optarg);
	    if (sl) {
		sl->active = TRUE;
	    } else {
		/* insert a new server in serverlist */
		sl = (struct serverlist *)
		    critmalloc(sizeof(struct serverlist),
			       "allocating space for server name");

		sl->name = critstrdup(optarg, "main");
		/* if port definition is present, cut it off */
		if ((p = strchr(sl->name, ':')) != NULL) {
		    *p = '\0';
		}
		sl->descriptions = TRUE;
		sl->next = servers;
		sl->timeout = 30;	/* default 30 seconds */
		sl->port = 0;	/* default port */
		/* if there is a port specification, override default: */
		if ((p = strchr(optarg, ':')) != NULL) {
		    p++;
		    if (p && *p) {
			sl->port = strtol(p, NULL, 10);
		    }
		}
		sl->username = NULL;
		sl->password = NULL;
		sl->active = TRUE;
		servers = sl;
	    }
	} else if ((option == 'N') && optarg && strlen(optarg)) {
	    newsgrp = critstrdup(optarg, "main");
	} else if ((option == 'M') && optarg && strlen(optarg)) {
	    msgid = critstrdup(optarg, "main");
	} else if (option == 'n') {
	    noexpire = 1;
	} else if (option == 'f') {
	    forceactive = 1;
	} else if (option == 'P') {
	    if (!msgid)
		postonly = 1;
	} else if (option == 'H') {
	    if (!msgid && testheaderbody(option))
		headerbody = 1;
	} else if (option == 'B') {
	    if (!msgid && testheaderbody(option))
		headerbody = 2;
	} else {
	    usage();
	    exit(EXIT_FAILURE);
	}
	debug = debugmode;
    }

    if (!servers) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "no servers found in configuration file.");
	exit(EXIT_FAILURE);
    }

    /* check compatibility of options */
    if (msgid) {
	if (delaybody) {
	    fprintf(stderr, "Option -M used: "
		    "ignored configuration file setting \"delaybody\".\n");
	    delaybody = 0;
	}
	if (headerbody) {
	    fprintf(stderr, "Option -M used: ignored options -H or -B.\n");
	    headerbody = 0;
	}
	if (forceactive) {
	    fprintf(stderr, "Option -M used: ignored option -f.\n");
	    forceactive = 0;
	}
	if (postonly) {
	    fprintf(stderr, "Option -M used: ignored option -P.\n");
	    postonly = 0;
	}
	if (newsgrp) {
	    fprintf(stderr, "Option -M used: ignored option -N.\n");
	    free(newsgrp);
	    newsgrp = NULL;
	}
    }
    if (newsgrp) {
	if (forceactive) {
	    fprintf(stderr, "Option -N used: ignored option -f.\n");
	    forceactive = 0;
	}
	if (postonly) {
	    fprintf(stderr, "Option -N used: ignored option -P.\n");
	    postonly = 0;
	}
    }
    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	   "%s: verbosity level is %d; debugging level is %d",
	   version, verbose, debugmode);
    if (noexpire) {
	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "Not automatically unsubscribing unread newsgroups.");
    }
    /* If fetchnews should post only, no lockfile or filters are required.
     * It is also sensible to check if there is anything to post when
     * invoked with -P; otherwise quit immediately.
     */
    if (!postonly) {
	/* Since the nntpd can create a lockfile for a short time, we
	 * attempt to create a lockfile during a five second time period,
	 * hoping that the nntpd will release the lock file during that
	 * time (which it should).
	 */
	if (signal(SIGALRM, sigcatch) == SIG_ERR)
	    fprintf(stderr, "Cannot catch SIGALARM.\n");
	else if (setjmp(jmpbuffer) != 0) {
	    ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
		   "lockfile %s exists, abort", lockfile);
	    exit(EXIT_FAILURE);
	}
	alarm(5);
	if (lockfile_exists(TRUE))
	    exit(EXIT_FAILURE);
	alarm(0);

	if (!forceactive)
	    rereadactive();

	feedincoming();

	readfilter(filterfile);
	if (!noexpire)
	    checkinteresting();
    } else {
	if (!checkforpostings())
	    exit(0);
    }
    if (signal(SIGTERM, sigcatch) == SIG_ERR)
	fprintf(stderr, "Cannot catch SIGTERM.\n");
    else if (signal(SIGINT, sigcatch) == SIG_ERR)
	fprintf(stderr, "Cannot catch SIGINT.\n");
    else if (signal(SIGUSR1, sigcatch) == SIG_ERR)
	fprintf(stderr, "Cannot catch SIGUSR1.\n");
    else if (signal(SIGUSR2, sigcatch) == SIG_ERR)
	fprintf(stderr, "Cannot catch SIGUSR2.\n");
    else if (setjmp(jmpbuffer) != 0) {
	servers = NULL;		/* in this case, jump the while ... loop */
    }

    while (servers) {
	current_server = servers;
	if (current_server->active) {
	    if (do_server(msgid, lastrun, newsgrp))
		continue;	/* no other servers have to be queried */
	}
	servers = servers->next;
    }

    signal(SIGINT, SIG_IGN);
    fflush(stdout);		/* to avoid double logging of stuff */

    if (!postonly) {
	writeactive();
	fflush(stdout);		/* to avoid double logging of stuff */
    }
/*    delposted(starttime); *//* FIXME */
    did_not_fork = 0;
    if (!postonly) {
	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "%lu articles fetched, %lu killed, in %ld seconds",
	       globalfetched, globalkilled, time(0) - starttime);

	switch (fork()) {
	case -1:		/* problem */
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "fork: %m, running on parent schedule");
	    did_not_fork = 1;
	    break;
	case 0:		/* child */
	    setsid();
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "Fixing XOVER");
	    fixxover();
	    unlink(lockfile);
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "Background process finished");
	    exit(0);
	default:		/* parent */
	    break;
	}
    }
    if (did_not_fork) {
	fixxover();
	unlink(lockfile);
    }
/*    delposted(starttime);  *//* FIXME */
    wait(0);
    freexover();
    freeactive();
    if (filter)
	freeallfilter(filter);
    freelocal();
    freeconfig();
    exit(0);
}
