/**
 * \file fetchnews.c
 *
 * Post articles to and get news from upstream server(s)
 * See AUTHORS for copyright holders and contributors.
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "get.h"
#include "critmem.h"
#include "ln_log.h"
#include "mastring.h"
#include "format.h"
#include "msgid.h"
#include "groupselect.h"
#include "fetchnews.h"

#include <sys/types.h>
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
#include <unistd.h>
#include <utime.h>
#include <assert.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static unsigned long globalfetched = 0;
static unsigned long globalhdrfetched = 0;
static unsigned long globalkilled = 0;
static unsigned long globalposted = 0;
static unsigned long groupfetched;
static unsigned long groupkilled;
static sigjmp_buf jmpbuffer;
static volatile sig_atomic_t canjump;
static time_t now;

/* Variables set by command-line options which are specific for fetchnews */
static unsigned long extraarticles = 0;		/* go back in upstream high mark and try refetching */
static unsigned int throttling = 0;		/* pause between streaming operations */
						/* the higher the value, the less bandwidth is used */
static int noexpire = 0;	/* if 1, don't automatically unsubscribe newsgroups */

/* fetchnews major operation.  Default: do everything */
#define FETCH_ARTICLE 1		/* get normal group articles */
#define FETCH_HEADER  2		/* get delaybody group pseudo heads */
#define FETCH_BODY    4		/* get delaybody group articles */
#define FETCH_POST    8		/* post to upstream */
static int action_method = FETCH_ARTICLE|FETCH_HEADER|FETCH_BODY|FETCH_POST;
static const char *action_description[] = {
    "get articles", "get headers", "get bodies", "post articles"
};

static struct stringlisthead *msgidlist = NULL;	/* list of Message-IDs to get (specify with -M) */
static struct stringlisthead *nglist = NULL;	/* newsgroups patterns to fetch */
static struct serverlist *only_server = NULL;		/* servers when -S option is given */

static void
ignore_answer(FILE * f)
{
    char *l;

    while (((l = mgetaline(f)))) {
	if (!strcmp(l, "."))
	    break;
    }
}

/**
 * generate name for upstream server info file.
 * \return a malloc()ed copy of the string, caller freed.
 */
static char *
server_info(const char *spool, const char *server,
	    const unsigned short port, const char *suffix)
{
    mastr *s = mastr_new(LN_PATH_MAX);
    char *res;
    char portstr[20] = "";	/* RATS: ignore */

    if (port) {
	portstr[0] = ':';
	str_ulong(portstr + 1, port);
    }

    mastr_vcat(s, spool, "/leaf.node/", server, portstr, suffix, NULL);

    res = critstrdup(mastr_str(s), "server_info");
    mastr_delete(s);
    return res;
}

static void sigcatch(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
	if (canjump == 0)
	    return;
	canjump = 0;
	signal(SIGALRM, SIG_IGN);	/* do not let mgetaline timeout interfere! */
	alarm(0);
	siglongjmp(jmpbuffer, signo);
    } else if (signo == SIGUSR1)
	verbose++;
    else if (signo == SIGUSR2)
	verbose--;
}

/* add the group names given on the cmdline to the interesting rbtree */
static void
add_fetchgroups(void)
{
    struct stringlistnode *sl;
    char *s;
    const char *t;

    if (nglist)
	for (sl=nglist->head; sl->next; sl=sl->next) {
	    /* do not add wildcards */
	    if (!strpbrk(sl->string, "\\*?[")) {
		s = critstrdup(sl->string, "add_fetchgroups");
		t = addtointeresting(s);
		if (t != NULL && t != s)	/* NULL: OOM, t!=s: repeated entry */
		    free(s);
	    }
	}
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n"
	    "fetchnews [GLOBAL OPTIONS] [-BfHnPR] [-t #] [-x #] [-S server[:port]]\n"
	    "          [-N {newsgroup|group.pattern}] [-M message-id]\n"
	    "options are:\n");
    fprintf(stderr,
	    GLOBALOPTLONGHELP);
    fprintf(stderr,
	    "    -B             - get article bodies in delaybody groups\n"
	    "    -f             - force reload of groupinfo file\n"
	    "    -H             - get article headers in delaybody groups\n"
	    "    -n             - switch off automatic unsubscription of groups\n");
    fprintf(stderr,
	    "    -P             - post only, don't get new articles\n"
	    "    -M message-id  - get article by Message-ID\n"
	    "    -N newsgroup   - get only articles in \"newsgroup\"\n"
	    "    -N group.pattern get articles from all interesting groups\n"
	    "                     matching the wildcard \"group.pattern\"\n");
    fprintf(stderr,
	    "    -R             - get articles in non delaybody groups\n"
	    "    -S server      - only get articles from \"server\"\n"
	    "    -t delay       - wait \"delay\" seconds between articles\n"
	    "    -x extra       - go \"extra\" articles back in upstream history\n");
    fprintf(stderr,
	    "Setting none of the options\n"
	    "    -B -H -P -R\n"
            "is equivalent to setting all of them, unless [-M message-id] is used.\n"
            "Options [-S server] and -f are mutually exclusive.\n"
	    "Options [-S server], [-M message-id] and [-N newsgroup] may be repeated.\n"
	    "Articles specified by Message-ID will always be fetched as a whole,\n"
	    "no matter if they were posted to a delaybody group.\n"
	    "\n");
}

/**
 * Split and convert port to long if the argument
 * for the -S option is given as server:port.
 * \return "port" or -1 in case of errors.
 *
 * contributed by Robert Grimm
 *
 * NOTE: This function modifies its first argument (strtok)!
 */
static long
split_serverarg(char *p, char sep)
{
    char *s[3], *t, delim[] = { '\0', '\0' };
    long port = 0;
    int i = 0;

    delim[0] = sep;
    if (strchr(p, sep)) {
	s[i] = strtok(p, delim);
	while (s[i]) {
	    if (i > 1)
	        return -1;

	    s[++i] = strtok(NULL, delim);
	}
	if (s[1]) {
	    port = strtol(s[1], &t, 10);
	    if (*t || t == s[1])
		return -1;
	}
	if (s[0] != p)
	    return -1;
    }
    return port > 65535 ? -1 : port;
}

/**
 * parse fetchnews command line
 * \return
 * -  0 for success
 * - -1 for failure
 */
static int
process_options(int argc, char *argv[], int *forceactive, char **conffile)
{
    int option;
    char *p;
    struct serverlist *sl = NULL;
    /** These flags (courtesy of Robert Grimm) become true if two
     * mutually exclusive arguments are encountered on the command line;
     * it builds upon the assumption that there is only one group of
     * flags of which only one can be active at any one time.
     */
    bool excl_arg_server = FALSE; /* Option -S server */
    bool excl_arg_active = FALSE; /* Option -f */

    /* state information */
    bool action_method_seen = FALSE;	/* BHPR */

    while ((option = getopt(argc, argv, GLOBALOPTS "BfHM:N:nPRS:t:x:")) != -1) {
	if (parseopt(argv[0], option, optarg, conffile))
	    continue;

	switch (option) {
	case 't':
	    throttling = strtoul(optarg, &p, 10);
	    if (*p || p == optarg) {
		usage();
		return -1;
	    }
	    break;
	case 'x':
	    extraarticles = strtoul(optarg, &p, 10);
	    if (*p || p == optarg) {
		usage();
		return -1;
	    }
	    break;
	case 'S':
	    {
		long portnr = 0;
		char s = ':';

		if (excl_arg_active) { /* -S and -f are mutually exclusive */
		    usage();
		    return -1;
		}
		excl_arg_server = TRUE;

		p = critstrdup(optarg, "processoptions");
		if ((portnr = split_serverarg(p, s)) < 0) {
		    usage();
		    return -1;
		}
		sl = create_server(p, (unsigned short)portnr);
		sl->next = only_server;
		only_server = sl;
		free(p);
	    }
	    break;
	case 'N':
	    initlist(&nglist);
	    appendtolist(nglist, optarg);
	    break;
	case 'M':
	    if (!msgidlist) {
		*forceactive = 0;	/* don't load active */
	    }
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = 0;	/* don't do anything else */
	    }
	    initlist(&msgidlist);
	    appendtolist(msgidlist, optarg);
	    break;
	case 'n':
	    noexpire = 1;
	    break;
	case 'f':
	    if (excl_arg_server) { /* -S and -f are mutually exclusive */
		usage();
		return -1;
	    }
	    excl_arg_active = TRUE;

	    *forceactive = 1;
	    break;
	case 'B':
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = FETCH_BODY;
	    } else
		action_method |= FETCH_BODY;
	    break;
	case 'H':
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = FETCH_HEADER;
	    } else
		action_method |= FETCH_HEADER;
	    break;
	case 'P':
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = FETCH_POST;
	    } else
		action_method |= FETCH_POST;
	    break;
	case 'R':
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = FETCH_ARTICLE;
	    } else
		action_method |= FETCH_ARTICLE;
	    break;
	default:
	    usage();
	    return -1;
	}
    }
    return 0;
}

static void
freeoptions(void)
{
    freelist(msgidlist);
    freelist(nglist);
}


static void
print_fetchnews_mode(/*@observer@*/ const char *myname)
{
    unsigned i, j;
    mastr *s = mastr_new(LN_PATH_MAX);

    mastr_vcat(s, myname, " mode: ", NULL);
    for (i = 0, j = 0; i<sizeof action_description / sizeof *action_description; ++i) {
	if (action_method & (1<<i)) {
	    if (j)
		mastr_cat(s, ", ");
	    mastr_cat(s, action_description[i]);
	    j++;
	}
    }
    if (msgidlist != NULL) {
	if (j)
	    mastr_cat(s,  ", ");
	mastr_cat(s,  "get messages by ID");
    }
    ln_log(LNLOG_SINFO, LNLOG_CTOP, "%s", mastr_str(s));
    mastr_delete(s);
}

/**
 * check whether any of the newsgroups is on server
 * \return
 * - TRUE if yes
 * - FALSE otherwise
 */
static int
isgrouponserver(const struct serverlist *cursrv, char *newsgroups)
{
    char *p, *q;
    int retval = FALSE;

    if ((p = newsgroups)) {
	do {
	    SKIPLWS(p);
	    q = strchr(p, ',');
	    if (q)
		*q++ = '\0';
	    if (gs_match(cursrv -> group_pcre, p)) {
		putaline(nntpout, "GROUP %s", p);
		if (nntpreply(cursrv) == 211)
		    retval = TRUE;
	    }
	    p = q;
	} while (q && !retval);
    }
    return retval;
}

/**
 * check whether message-id is on server
 * \return
 * - TRUE if yes
 * - FALSE otherwise
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
	l = mgetaline(nntpin);
	if (l && get_long(l, &a) == 1) {
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
    l = mgetaline(nntpin);
    if (l && get_long(l, &a) == 1 && a == 221) {
	ignore_answer(nntpin);
	return TRUE;
    } else {
	return FALSE;
    }
}

/**
 * Get a single article and apply filters.
 *
 * XXX FIXME: do we want a "reconnect to current server and continue"
 * state when we're out of synch? We'd need to track if we're making
 * progress so we don't loop on the same b0rked article for instance.
 *
 * \return
 *  - -2: server disconnected or OS error in store, abort the fetch
 *  - -1: did not get a 22X reply, continue the fetch
 *  - 0: other error receiving article
 *  - 1: success, article number on upstream server stored in artno
 *
 */
static int
getarticle(/*@null@*/ struct filterlist *filtlst, unsigned long *artno,
    int delayflg)
{
    char *l;
    int reply = 0, argcount;

    l = mgetaline(nntpin);
    if (!l) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "Server went away when it should send an article.");
	return -2;
    }

    if (((argcount = sscanf(l, "%3d %lu", &reply, artno)) < 2)
	    || (reply / 10 != 22)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
	       "Wrong reply to ARTICLE command: \"%s\"", l);
	/* don't complain if artno missing,
	 * 423 replies for instance don't include it, but are non-fatal. */
	if (reply >= 500 && reply < 600)
	    return -2;		/* fatal error */
	/* recoverable error */
	return -1;
    }

    switch (store_stream(nntpin, 1, (filtermode & FM_HEAD ? filtlst : NULL),
			 -1, delayflg)) {
    case 2: /* no valid newsgroups */
    case 1: /* killfilter */
    case -2: /* duplicate */
	groupkilled++;
	return 1;
    case 0:
	groupfetched++;
	return 1;
    case -1:
	return -2;
    default:
	return 0;
    }
}

/**
 * get an article by message id
 */
static int
getbymsgid(const char *msgid, int delayflg)
{
    unsigned long artno;
    putaline(nntpout, "ARTICLE %s", msgid);
    return (getarticle(NULL, &artno, delayflg) > 0) ? TRUE : FALSE;
}

/**
 * get a list of message-ids, remove successfully fetched ids
 * \return
 * -  0 if all articles successfully loaded
 * - -1 if articles outstanding
 * - -2 if server error, disconnect
 */
static int
getmsgidlist(struct stringlisthead *first)
{
    struct stringlistnode *slp;
    struct stringlistnode *next;

    if (first == NULL)	/* consistency check */
	return -1;

    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
	   "Getting articles specified by Message-ID");
    groupfetched = 0;
    groupkilled = 0;

    /* remove MIDs of articles we already have
     * as well as successfully downloaded articles
     */
    slp = first->head;
    for (slp = first->head; (next = slp->next); slp = next) {
	char *mid = slp->string;

	if (ihave(mid)) {
	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
		    "I have Article %s already", mid);
	    removefromlist(slp);
	    continue;
	}
        if (getbymsgid(mid, 0)) {
	    removefromlist(slp);
	    continue;
	}
    }
    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
	    "%lu articles fetched by Message-ID, %lu killed",
	    groupfetched, groupkilled);
    globalfetched += groupfetched;
    globalkilled += groupkilled;
    return is_listempty(first) ? 0 : -1;
}


#if 0
/*
 * get articles by using NEWNEWS
 * returns last number of processed article or 0 if NEWNEWS didn't work
 *
 * unfortunately DNews returns 230 even if NEWNEWS is disabled
 */
static unsigned long
fn_donewnews(struct newsgroup *g, time_t lastrun)
{
    char timestr[64];

    ln_log(LNLOG_SERR, LNLOG_CTOP,
	   "WARNING: fn_donewnews called, not yet implemented, aborting");
    abort();
    /* these two lines borrowed from nntpactive() */
#ifdef NOTYET
    strftime(timestr, 64, "%Y%m%d %H%M00", gmtime(&lastrun));
    ln_log(LNLOG_SINFO, LNLOG_CGROUP, "doing NEWNEWS %s %s",
	   g->name, timestr + 2);
    putaline(nntpout, "NEWNEWS %s %s", g->name, timestr + 2);
    if (nntpreply(current_server) != 230) {
	/* FIXME: if the upstream uses NNTPcache v2.4.0b5, it may send a
	 * line with a single dot after a 502 reply in violation of
	 * RFC-977 */
	return 0;		/* NEWNEWS not supported or something going wrong */
    }
/* #ifdef NOTYET */
    while ((l = mgetaline(nntpin)) && strcmp(l, ".")) {
	/* get a list of message ids: put them into a stringlist and
	   request them one by one */
    }
    return 1;
#else
    ignore_answer(nntpin);
    return 0;
#endif
}
#endif

/**
 * Get bodies of messages that have marked for download.
 * The group must already be selected at the remote server and
 * the current directory must be the one of the group.
 */
static void
getmarked(struct newsgroup *group)
{
    FILE *f;
    char *l;
    struct stringlisthead *failed = NULL;
    struct stringlistnode *ptr = NULL;
    mastr *fname = mastr_new(LN_PATH_MAX);

    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "Getting bodies of marked messages for group %s ...", group->name);
    (void)mastr_vcat(fname, spooldir, "/interesting.groups/", group->name,  NULL);
    if (!(f = fopen(mastr_str(fname), "r"))) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
	       "Cannot open %s for reading: %m", mastr_str(fname));
	mastr_delete(fname);
	return;
    }
    initlist(&failed);
    while ((l = getaline(f))) {
	char *fi[4];
	struct stat dummy1;
	time_t ts, expire, limit;

	if (str_nsplit(fi, l, " ", COUNT_OF(fi)) < 2) {
	    /* skip malformatted line */
	    ln_log(LNLOG_SWARNING, LNLOG_CGROUP,
		    "warning: skipping malformatted "
		    "interesting.groups/%s line \"%s\"",
		    group->name, l);
	    continue;
	}

	/* check time stamp of mark, if too old, expire */
	ts = fi[2] ? strtoul(fi[2], NULL, 10) : 0;
	if (ts < 10)
	    /* compatibility with 20040818a that wrote a retry counter */
	    ts = time(NULL);

	/* interesting.group article expire:
	 * - if timeout_delaybody is set, use this (in hours)
	 * - if unset, but groupexpire is set, use this
	 * - if unset, use expire
	 * timeout_delaybody will be limited to expire
	 */
	expire = lookup_expire(group->name);
	if (expire <= 0)
	    expire = default_expire;

	if (timeout_delaybody > 0)
	    limit = time(NULL) - 3600 * timeout_delaybody;
	else
	    limit = expire;

	if (expire > limit)
	    limit = expire;

	if (ts < limit) {
	    /* expired */
	    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
		    "interesting.groups/%s: mark for %s %s has expired",
		    group->name, fi[1], fi[0]);
	    continue;
	}

	if (stat(fi[1], &dummy1)) {
	    /* file not existent -> skip */
	    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
		    "interesting.groups/%s: article %s %s not present, stat failed: %m",
		    group->name, fi[1], fi[0]);
	    continue;
	}

	if (!getbymsgid(fi[0], 2)) {
	    /* mark article for retry */
	    mastr *tmp = mastr_new(200);
	    mastr_vcat(tmp, fi[0], " ", fi[1], " ", fi[2], "\n", NULL);
	    appendtolist(failed, mastr_str(tmp));
	    mastr_delete(tmp);
	}
    }
    fclose(f);
    /* XXX FIXME: overwriting is a bit dangerous and can lose marks
     * however creating a new file changes the ctime which we must avoid */
    /* write back ids of all articles which could not be retrieved */
    if (!(f = fopen(mastr_str(fname), "w")))
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Cannot open %s for writing: %m", mastr_str(fname));
    else {
	int eflag = 0;
	setbuf(f, NULL); /* make unbuffered */
	ptr = failed->head;
	while (ptr->next) {
	    if (EOF == fputs(ptr->string, f)) {
		eflag = 1;
		break;
	    }
	    ptr = ptr->next;
	}
	if (fclose(f)) eflag = 1;
	if (eflag)
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		    "Write error on %s: %m", mastr_str(fname));
    }
    freelist(failed);
    mastr_delete(fname);
}

static int parseulong(const char **in, /*@out@*/ unsigned long *var)
{
    int valid = 0, oflow = 0;
    unsigned long value = 0;

    for(;;) {
	unsigned int d;
	if (!isdigit((unsigned char)**in)) {
	    if (valid)
		*var = value;
	    if (oflow)
		*var = ULONG_MAX, errno = ERANGE;
	    return valid - oflow;
	}
	valid = 1;
	d = **in - '0';
	(*in)++;
	if (value > ULONG_MAX / 10u || (value == ULONG_MAX / 10u && d > ULONG_MAX % 10u)) {
	    oflow = 1;
	}
	value = value * 10 + d;
    }
}

static int
parsegroupreply(const char **s, unsigned long *code, unsigned long *number,
	unsigned long *first, unsigned long *last)
{
    if (!parseulong(s, code)) return 0;
    SKIPLWS(*s);
    if (!parseulong(s, number)) return 0;
    SKIPLWS(*s);
    if (!parseulong(s, first)) return 0;
    SKIPLWS(*s);
    if (!parseulong(s, last)) return 0;
    SKIPLWS(*s);
    return 1;
}

/**
 * calculate first and last article number to get
 * \return
 * -  0 for error
 * -  1 for success
 * - -1 if group is not available at all
 * - -2 for a fatal error on current server (skip to next)
 */
static int
getfirstlast(struct serverlist *cursrv, struct newsgroup *g, unsigned
	long *first, unsigned long *last, int delaybody_this_group)
{
    unsigned long h, window, u;
    long n;
    char *l, *t;

    if (!gs_match(cursrv -> group_pcre, g->name))
	return 0;

    putaline(nntpout, "GROUP %s", g->name);
    n = newnntpreply(cursrv, &l);
    if (!l)
	return 0;

    if (get_long(l, &n) && n == 480) {
	return -1;
    }

    if (get_long(l, &n) && n == 411) {
	/* group not available on server */
	return -1;
    }

    t = l;
    /*                               input nnn   #   first   last */
    if (!parsegroupreply((const char **)&t, &u, &h, &window, last)) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "%s: cannot parse reply to \"GROUP %s\": \"%s\"",
		cursrv->name, g->name, l);
	return -2;
    }

    if (u != 211) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "%s: protocol error in response to GROUP %s: \"%s\", must be 211 or 411",
		cursrv->name, g->name, l);
	return 0;
    }

    if (h == 0 || window > *last) {
	/* group available but no articles on server */
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
	       "%s: backing up %s from %lu to %lu", cursrv->name, g->name,
	       h, *first);
	*first = h;
    }

    if (*first > *last + 1) {
	const long maximum_decrease = 10;
	
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: %s last seen %s was %lu, server now has %lu - %lu",
	       cursrv->name, g->name,
	       delaybody_this_group ? "header" : "article",
	       *first, window, *last);
	if (*first > (*last + maximum_decrease)) {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: switched upstream servers for %s? upstream bug? %lu > %lu",
		   cursrv->name, g->name, *first, *last);
	    *first = window;	/* check all upstream articles again */
	    if ((initiallimit) && (*last - *first > initiallimit))
		*first = *last - initiallimit + 1;
	} else {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: rampant spam cancel in %s? upstream bug? %lu > %lu",
		   cursrv->name, g->name, *first - 1, *last);
	    *first = *last - maximum_decrease; /* re-check last N
						  upstream articles */
	}
    }
    if (initiallimit && (*first == 1) && (*last - *first > initiallimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: %s: skipping %s %lu-%lu inclusive (initial limit)",
	       cursrv->name, g->name, delaybody_this_group ? "headers" : "articles",
	       *first, *last - initiallimit);
	*first = *last - initiallimit + 1;
    }
    if (artlimit && (*last + 1 - *first > artlimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: %s: skipping %s %lu-%lu inclusive (article limit)",
	       cursrv->name, g->name, delaybody_this_group ? "headers" : "articles",
	       *first, *last - artlimit);
	*first = *last + 1 - artlimit;
    }
    if (window < *first)
	window = *first;
    if (window < 1)
	window = 1;
    *first = window;
    if (*first > *last) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP, "%s: %s: no new %s",
		cursrv->name, g->name,
		delaybody_this_group ? "headers" : "articles");
	return 0;
    }
    return 1;
}


/** create pseudo article header from xover entries */
static /*@only@*/ mastr *
create_pseudo_header(const char *subject, const char *from,
	const char *date, const char *messageid, const char *references,
	char **newsgroups_list, int num_groups, const char *bytes, const char *lines, const char *xref)
{
    int n;
    mastr *s = mastr_new(2048);
    mastr_vcat(s, "From: ", from,
	        "\nSubject: ", subject,
		"\nMessage-ID: ", messageid,
		"\nReferences: ", references,
		"\nDate: ", date,
		NULL);
    if (num_groups)
	mastr_vcat(s, "\nNewsgroups: ", newsgroups_list[0], NULL);
    for (n = 1; n < num_groups; n++)
	mastr_vcat(s, ",", newsgroups_list[n], NULL);
    mastr_vcat(s, "\nPath: fake-delaybody-path!not-for-mail\n", NULL);
    if (lines)
	mastr_vcat(s, "Lines: ", lines, "\n", NULL);
    if (bytes)
	mastr_vcat(s, "Bytes: ", bytes, "\n", NULL);
    if (xref)
	mastr_vcat(s, xref, "\n", NULL); /* Xref headers usually come
					    with their name in XOVER */
    return s;
}

/** for delaybody: store pseudo article header
 *  \return
 *  - -1 for error
 *  -  0 for sucess
 */
static int
store_pseudo_header(mastr *s)
{
    int rc = 0;
    int tmpfd;
    mastr *tmpfn = mastr_new(LN_PATH_MAX);

    (void)mastr_vcat(tmpfn, spooldir, "/temp.files/delaypseudo_XXXXXXXXXX", NULL);
    tmpfd = safe_mkstemp(mastr_modifyable_str(tmpfn));
    if (tmpfd < 0) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		"store_pseudo_header: error in mkstemp(\"%s\"): %m",
		mastr_str(tmpfn));
	rc = -1;
	goto out;
    }
    if (write(tmpfd, mastr_str(s), mastr_len(s)) < (ssize_t)mastr_len(s)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		"store_pseudo_header: write failed: %m");
	rc = -1;
    }

    if (log_fsync(tmpfd)) rc = -1;
    if (log_close(tmpfd)) rc = -1;
    if (rc == -1) goto out;

    if (store(mastr_str(tmpfn), 0, 0, 1) == 0) {
	(void)log_unlink(mastr_str(tmpfn), 0);
    } else {
	rc = -1;
    }
out:
    mastr_delete(tmpfn);
    return rc;
}

/**
 * get headers of articles with XOVER and return a stringlist of article
 * numbers to get (or number of pseudo headers stored)
 * \return
 * - -1 for error
 * - -2 if XOVER was rejected
 */
static long
fn_doxover(struct stringlisthead *stufftoget,
	unsigned long first, unsigned long last,
	/*@null@*/ struct filterlist *filtlst, char *groupname)
{
    char *l, *xref_scratch;
    unsigned long count = 0, dupes = 0, seen = 0;
    long reply;
    int delaybody_this_group = delaybody_group(groupname);

    putaline(nntpout, "XOVER %lu-%lu", first, last);
    l = mgetaline(nntpin);
    if (l == NULL || (!get_long(l, &reply)) || (reply != 224)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XOVER command: %s", l ? l : "(null)");
	return -2;
    }
    while ((l = mgetaline(nntpin)) && strcmp(l, ".")) {
	char *xover[20];	/* RATS: ignore */
	char *artno, *subject, *from, *date, *messageid;
	char *references, *lines, *bytes, *xref;
	char **newsgroups_list = NULL;
	int num_groups;

	seen ++;
	if (abs(str_nsplit(xover, l, "\t", sizeof(xover) / sizeof(xover[0]))) <
	    8) {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP,
		   "read broken xover line, too few fields: \"%s\", skipping",
		   l);
	    goto next_over;
	}

	/* format of an XOVER line is usually:
	   article number, subject, author, date, message-id, references,
	   byte count, line count, xref (optional) */
	artno = xover[0];
	subject = xover[1];
	from = xover[2];
	date = xover[3];
	messageid = xover[4];
	references = xover[5];
	bytes = xover[6];
	lines = xover[7];
	xref = xover[8];
	
	xref_scratch = xref ? critstrdup(xref, "fn_doxover") : NULL;

	/* is there an Xref: header present as well? */
	if (xover[8] == NULL ||
	    (num_groups = xref_to_list(xref_scratch,
				       &newsgroups_list, NULL, 0)) == -1) {
	    /* newsgroups filling by hand */
	    num_groups = 1;
	    newsgroups_list = (char **)critmalloc(num_groups * sizeof *newsgroups_list, "doxover");
	    newsgroups_list[0] = groupname;
	}

	if ((filtermode & FM_XOVER) || delaybody_this_group) {
	    mastr *s;

	    s = create_pseudo_header(subject, from, date, messageid,
		    references, newsgroups_list, num_groups, bytes, lines,
		    xref);

	    if (xref_scratch)
		free(xref_scratch);

	    if (filtlst && killfilter(filtlst, mastr_str(s))) {
		groupkilled++;
		ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
			"article %s %s rejected by filter (XOVER)", artno,
			messageid);
		if (debugmode & DEBUG_FILTER)
		    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
			    "Headers: %s", mastr_str(s));

		/* filter pseudoheaders */
		mastr_delete(s);
		goto next_over;
	    }
	    if (ihave(messageid)) {
		/* we have the article already */
		dupes++;
		mastr_delete(s);
		goto next_over;
	    }

	    if (delaybody_this_group) {
		/* write pseudoarticle */
		if (store_pseudo_header(s) == 0)
		    count++;
	    } else {
		count++;
		appendtolist(stufftoget, artno);
	    }
	    mastr_delete(s);
	} else {
	    count++;
	    appendtolist(stufftoget, artno);
	}
next_over:
	free_strlist(xover);
	if (newsgroups_list)
	  free(newsgroups_list);
    }

    {
	int rc = count;

	if (l && strcmp(l, ".") == 0) {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP, "%s: XOVER: %lu seen, %lu I have, "
		    "%lu filtered, %lu to get",
		    groupname, seen, dupes, groupkilled, count);
	} else {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "%s: XOVER: reply was mutilated",
		    groupname);
	    rc = -1;
	}
	globalkilled += groupkilled;
	groupkilled = 0;

	return rc;
    }
}

/**
 * use XHDR to check which articles to get. This is faster than XOVER
 * since only message-IDs are transmitted, but you lose some features
 * \return
 * - -1 for error
 * - -2 if XHDR was rejected
 */
static long
fn_doxhdr(struct stringlisthead *stufftoget, unsigned long first,
	unsigned long last)
{
    char *l;
    unsigned long count = 0;
    long reply;

    putaline(nntpout, "XHDR message-id %lu-%lu", first, last);
    l = mgetaline(nntpin);
    if (l == NULL || (!get_long(l, &reply)) || (reply != 221)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XHDR command: %s", l ? l : "(null)");
	return -2;
    }
    while ((l = mgetaline(nntpin)) && strcmp(l, ".")) {
	/* format is: [# of article] [message-id] */
	char *t;

	t = l;
	SKIPWORD(t);
	if (ihave(t))
	    continue;
	/* mark this article */
	count++;
	appendtolist(stufftoget, l);
    }
    if (l && strcmp(l, ".") == 0)
	return count;
    else
	return -1;
}

/*
 * get articles
 */

static /*@dependent@*/ const char *
chopmid(/*@unique@*/ const char *in)
{
    /* chops off second word */
    static char buf[1024];
    char *j = buf, *k;
    (void)mastrncpy(buf, in, sizeof(buf));
    SKIPLWS(j);
    k = j;
    while (isdigit((unsigned char)*k))
	k++;
    *k = '\0';			/* chop off message-ID in case we used XHDR */
    return buf;
}

/**
 * get all articles in a group, with pipelining NNTP commands
 * \return false for an error that should cause fetchnews to give up on
 * the current server; true otherwise.
 */
static bool
getarticles(/*@null@*/ struct stringlisthead *stufftoget,
	long n /** number of articles to fetch */,
	/*@null@*/ struct filterlist *f)
{
    struct stringlistnode *p;
    long advance = 0, window;
    unsigned long artno_server = 0ul;
    long remain = sendbuf;

    p = stufftoget->head;
    window = (n < windowsize) ? n : windowsize;
    while (p->next || advance) {
	remain = sendbuf;
	/* stuff pipeline until TCP send buffer is full or window size
	 * is reached (preload, don't read anything) */
	while (p && advance < window) {
	    const char *c = chopmid(p->string);
	    if (!(advance == 0 || (remain > 0 && (unsigned long)remain > strlen(c) + 10)))
		break;
	    fprintf(nntpout, "ARTICLE %s\r\n", c = chopmid(p->string));
	    remain -= 10 + strlen(c);	/* ARTICLE + SP + CR + LF == 10 characters */
	    p = p->next;
	    advance++;
	    ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE, "sent ARTICLE %s command, "
		    "in pipe: %ld", c, advance);
	    if (throttling)
		sleep(throttling);
	}
	/* send the command batch */
	fflush(nntpout);
	/* now read articles and feed ARTICLE commands one-by-one */
	while (advance) {
	    int res = getarticle(f, &artno_server, 0);
	    if (res == -2)
		return FALSE;	/* disconnected server or store OS error */
	    /* FIXME: add timeout here and force window to 1 if it triggers */
	    if (p->next) {
		const char *c;
		fprintf(nntpout, "ARTICLE %s\r\n", c = chopmid(p->string));
		fflush(nntpout);
		p = p->next;
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "received article, sent ARTICLE %s command, "
		       "in pipe: %ld", c, advance);
		if (throttling)
		    sleep(throttling);
	    } else {
		advance--;
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "received article, in pipe: %ld", advance);
	    }
	}
    }
    return TRUE;
}

/**
 * fetch all articles for that group.
 * \return
 * - -2 to abort fetch from current server
 * - 0 for error or if group is unavailable
 * - otherwise last article number in that group
 */
static unsigned long
getgroup(struct serverlist *cursrv, struct newsgroup *g, unsigned long first)
{
    struct stringlisthead *stufftoget = NULL;
    struct filterlist *f = NULL;
    int x = 0;
    long outstanding = 0;
    unsigned long last = 0;
    int delaybody_this_group;
    int tryxhdr = 0;
    bool u;

    /* lots of plausibility tests */
    if (!g)
	return first;
    if (!is_interesting(g->name))
	return 0;
    if (!chdirgroup(g->name, TRUE))	/* also creates the directory */
	return 0;
    if (!gs_match(cursrv -> group_pcre, g->name))
	return 0;

    /* we don't care about x-posts for delaybody */
    delaybody_this_group = delaybody_group(g->name);

    /* get marked articles first */
    if (delaybody_this_group) {
        if (action_method & FETCH_BODY) {
	    groupfetched = 0;
	    groupkilled = 0;
	    getmarked(g);
	    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
		    "%s: %lu marked bodies fetched, %lu killed",
		    g->name, groupfetched, groupkilled);
	    globalfetched += groupfetched;
	    globalkilled += groupkilled;
	}
	if ((action_method & FETCH_HEADER) == 0) {
	    /* get only marked bodies, nothing else */
	    return first;
	}
    } else {
	if ((action_method & FETCH_ARTICLE) == 0)
	    return first;
    }

    if (!first)
	first = 1;

    if (g->first > g->last)
	g->first = g->last+1;

    x = getfirstlast(cursrv, g, &first, &last, delaybody_this_group);

    switch (x) {
    case 0:
	return first;
    case -1:
	return 0;
    case -2:
	return -2;
    default:
	break;
    }


    if (filter)
	f = selectfilter(g->name);

    /* use XOVER since it is often faster than XHDR. User may prefer
       XHDR if they know what they are doing and no additional information
       is requested */
    if (!cursrv->usexhdr || f || delaybody_this_group) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %lu %s %lu - %lu, using XOVER", g->name,
		(last - first + 1),
		delaybody_this_group ? "headers" : "articles",
		first, last);
	initlist(&stufftoget);
	outstanding = fn_doxover(stufftoget, first, last, f, g->name);

	/* fall back to XHDR only without filtering or delaybody mode */
	if (outstanding == -2 && !f && !delaybody_this_group)
	    tryxhdr = 1;
    } else {
	tryxhdr = 1;
    }

    if (tryxhdr) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %lu articles %lu - %lu, using XHDR", g->name,
	       (last - first + 1), first, last);
	initlist(&stufftoget);
	outstanding = fn_doxhdr(stufftoget, first, last);
    }

    switch (outstanding) {
    case -2:
	cursrv->usexhdr = 0;	/* disable XHDR */
	/*@fallthrough@*/ /* fall through to -1 */
    case -1:
	freefilter(f);
	freelist(stufftoget);
	return first;			/* error */
    case 0:
	freefilter(f);
	freelist(stufftoget);
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		"%s: all %s already there", g->name,
		delaybody_this_group ? "headers" : "articles");
	return last + 1;
    default:
	if (delaybody_this_group) {
	    globalhdrfetched += outstanding;
	    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
		   "%s: %ld pseudo headers fetched",
		   g->name, outstanding);
	    freefilter(f);
	    freelist(stufftoget);
	    return last + 1;
	}
    }

    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "%s: will fetch %ld articles", g->name, outstanding);

    groupfetched = 0;
    groupkilled = 0;

    u = getarticles(stufftoget, outstanding, f);
    freefilter(f);
    freelist(stufftoget);
    if (u == FALSE) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
		"%s: error fetching, proceeding to next server",
		g->name);
	return -2;
    }
    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
	   "%s: %lu articles fetched (to %lu), %lu killed",
	   g->name, groupfetched, g->last, groupkilled);
    globalfetched += groupfetched;
    globalkilled += groupkilled;

    return last + 1;
}

/** Split a line which is assumed in RFC-977 LIST format.  Puts a
 * pointer to the end of the newsgroup name into nameend (the caller
 * should then set this to '\\0'), and a pointer to the status character
 * into status.
 * \return
 * - 1 if the status character is valid
 * - 0 if the status character is invalid
 */
static int
splitLISTline(char *line, /*@out@*/ char **nameend, /*@out@*/ char **status)
{
    char *p = line;

    SKIPWORDNS(p);
    *nameend = p;		/* end of the group name */
    SKIPLWS(p);
    SKIPWORD(p);		/* last */
    SKIPWORD(p);		/* first */
    /* p now points to the status char */
    *status = p;
    switch (*p) {
    case 'y':
    case 'n':
    case 'm':
    case 'j':			/* j, = and x are for INN compatibility */
    case '=':
    case 'x':
	return 1;
    default:
	ln_log(LNLOG_SWARNING, LNLOG_CGROUP,
	       "bad status character in \"%s\", skipping", line);
	return 0;
    }
}

/**
 * get active file from cursrv
 * \returns 0 for success, non-0 for error.
 */
static int
nntpactive(struct serverlist *cursrv, int fa)
{
    struct stat st;
    char *l, *p, *q;
    struct stringlisthead *groups = NULL;
    struct stringlistnode *helpptr = NULL;
    mastr *s = mastr_new(LN_PATH_MAX);
    char timestr[64];		/* must store at least a date in YYMMDD HHMMSS format */
    char portstr[20];
    int reply = 0, error;
    unsigned long count = 0;
    unsigned long first, last;
    int forceact = fa;
    time_t last_update = 0;

    /* the simple upstream file is used to check the time of the last
     * update. The last:*:* file is used to check the time of the last
     * full-fetch (must not be older than timeout_active)
     */

    /* figure last update */
    p = server_info(spooldir, cursrv->name, cursrv->port, "");
    if (0 == stat(p, &st))
	last_update = st.st_mtime;
    else
	/* never fetched before */
	forceact = 1;
    free(p);

    str_ulong(portstr, cursrv->port);
    mastr_vcat(s, spooldir, "/leaf.node/last:", cursrv->name, ":", portstr, NULL);

    if (!forceact && (0 == stat(mastr_str(s), &st))) {
	initlist(&groups);

	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		"%s: checking for new newsgroups", cursrv->name);
	/* "%Y" and "timestr + 2" avoid Y2k compiler warnings */
	strftime(timestr, 64, "%Y%m%d %H%M%S", gmtime(&last_update));
	putaline(nntpout, "NEWGROUPS %s GMT", timestr + 2);
	if (nntpreply(cursrv) != 231) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER, "Reading new newsgroups failed");
	    mastr_delete(s);
	    freelist(groups);
	    return 1;
	}
	while ((l = mgetaline(nntpin)) && (strcmp(l, ".") != 0)) {
	    char *r;
	    p = l;
	    if (!splitLISTline(l, &p, &r))
		continue;
	    *p = '\0';
	    if (gs_match(cursrv->group_pcre, l)) {
		insertgroup(l, *r, 1, 0, time(NULL), NULL);
		appendtolist(groups, l);
		count++;
	    }
	}
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
		"%s: found %lu new newsgroups", cursrv->name, count);
	if (!l) {
	    /* timeout */
	    mastr_delete(s);
	    freelist(groups);
	    return 1;
	}
	mergegroups();		/* merge groups into active */
	if (count && cursrv->descriptions) {
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		    "%s: getting new newsgroup descriptions",
		    cursrv->name);
	    for (helpptr = groups->head; helpptr->next != NULL; helpptr = helpptr->next) {
		error = 0;
		putaline(nntpout, "LIST NEWSGROUPS %s", helpptr->string);
		reply = nntpreply(cursrv);
		if (reply == 215) {
		    l = mgetaline(nntpin);
		    if (!l) {
			mastr_delete(s);
			freelist(groups);
			return 1;
		    }
		    if (*l != '.') {
			p = l;
			CUTSKIPWORD(p);
			changegroupdesc(l, *p ? p : NULL);
			do {
			    l = mgetaline(nntpin);
			    error++;
			} while (l && *l && strcmp(l, "."));
			if (error > 1) {
			    cursrv->descriptions = 0;
			    ln_log(LNLOG_SWARNING, LNLOG_CSERVER,
				    "%s: warning: server does not process "
				    "LIST NEWSGROUPS %s correctly: use nodesc",
				    cursrv->name, helpptr->string);
			}
		    }
		}
	    }
	}
	freelist(groups);
    } else {    /* read new active */
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		"%s: getting newsgroups list", cursrv->name);
	putaline(nntpout, "LIST");
	if (nntpreply(cursrv) != 215) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER,
		    "%s: reading all newsgroups failed", cursrv->name);
	    mastr_delete(s);
	    return 1;
	}
	while ((l = mgetaline(nntpin)) && (strcmp(l, "."))) {
	    p = l;
	    if (!splitLISTline(l, &q, &p))
		continue;
	    *q = '\0';		/* cut out the group name */

	    /* see if the newsgroup is interesting.  if it is, and we
	       don't have it in groupinfo, figure water marks */
	    /* FIXME: save high water mark in .last.posting? */
	    first = 1;
	    last = 0;
	    if (gs_match(cursrv->group_pcre, l)) {
		if (is_interesting(l)
			&& (forceact || !(active && findgroup(l, active, -1)))
			&& chdirgroup(l, FALSE)) {
		    unsigned long c = 0;
		    first = ULONG_MAX;
		    last = 0;
		    if (getwatermarks(&first, &last, &c) || 0 == c) {
			/* trouble or empty group */
			first = 1;
			last = 0;
		    }
		}
		insertgroup(l, p[0], first, last, 0, NULL);
		count++;
	    }
	}
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		"%s: read %lu newsgroups", cursrv->name, count);

	mergegroups();

	if (cursrv->descriptions) {
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		    "%s: getting newsgroup descriptions", cursrv->name);
	    putaline(nntpout, "LIST NEWSGROUPS");
	    l = mgetaline(nntpin);
	    /* correct reply starts with "215". However, INN 1.5.1 is broken
	       and immediately returns the list of groups */
	    if (l) {
		char *tmp;

		reply = strtol(l, &tmp, 10);
		if ((reply == 215) && (*tmp == ' ' || *tmp == '\0')) {
		    l = mgetaline(nntpin);	/* get first description */
		} else if (*tmp != ' ' && *tmp != '\0') {
		    /* FIXME: INN 1.5.1: line already contains description */
		    /* do nothing here */
		} else {
		    ln_log(LNLOG_SERR, LNLOG_CSERVER,
			    "%s: reading newsgroups descriptions failed: %s",
			    cursrv->name, l);
		    mastr_delete(s);
		    return 1;
		}
	    } else {
		ln_log(LNLOG_SERR, LNLOG_CSERVER,
			"%s: reading newsgroups descriptions failed",
			cursrv->name);
		mastr_delete(s);
		return 1;
	    }
	    while (l && (strcmp(l, "."))) {
		p = l;
		CUTSKIPWORD(p);
		changegroupdesc(l, *p ? p : NULL);
		l = mgetaline(nntpin);
	    }
	    if (!l) {
		mastr_delete(s);
		return 1;		/* timeout */
	    }
	}
	/* touch file */
	{
	    int e = touch_truncate(mastr_str(s));
	    if (e < 0)
		ln_log(LNLOG_SERR, LNLOG_CGROUP, "cannot touch %s: %m", mastr_str(s));
	}
    }
    mastr_delete(s);
    return 0;
}

/* post article in open file f, return FALSE if problem, return TRUE if ok */
/* FIXME: add IHAVE */
static int
post_FILE(const struct serverlist *cursrv, FILE * f, char **line)
{
    int r;
    char *l;

    rewind(f);
    putaline(nntpout, "POST");
    r = newnntpreply(cursrv, line);
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
    *line = mgetaline(nntpin);
    if (!*line)
	return FALSE;
    if (0 == strncmp(*line, "240", 3))
	return TRUE;
    return FALSE;
}

/* these groups need not be fetched *again* */
static struct rbtree *done_groups = NULL;

/** get server high water mark
 *  \return 1 if parse error or group not found
 */
static unsigned long
get_old_watermark(const char *group, struct rbtree *upstream)
{
    char *t;
    const char *k;
    unsigned long a;

    if (upstream == NULL)
	return 1;

    k = (const char *)rbfind(group, upstream);

    if (k == NULL)
	return 1;
    SKIPWORD(k);
    if (*k == '\0')
	return 1;

    a = strtoul(k, &t, 10);
    if (!*t)
	return a;
    else
	return 1;
}


static void
remove_watermark(const char *group, struct rbtree *upstream,
	int dontcomplain)
{
    const char *k;

    if (upstream == NULL)
	return;
    k  = (const char *)rbdelete(group, upstream);
    if (k) {
	free((char *)k);		/* ugly but true */
    } else {
	if (!dontcomplain)
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "no watermark for group %s", group);
    }
}


/** process a given server/port,
 *  read upstream water marks and write back, using temporary file
 * \return
 * -  1 for success
 * -  0 otherwise
 */
static int
processupstream(struct serverlist *cursrv, const char *const server,
	const unsigned short port, int forceactive)
{
    FILE *f;
    const char *ng;			/* current group name */
    char *newfile, *oldfile;		/* temp/permanent server info files */
    RBLIST *r = NULL;			/* interesting groups pointer */
    struct rbtree *upstream;		/* upstream water marks */
    int rc = 0;				/* return value */
    int fault = 0;			/* if we skip the rest of the groups */

    /* read info */
    oldfile = server_info(spooldir, server, port, "");
    newfile = server_info(spooldir, server, port, "~");

    /* read old watermarks in rbtree */
    if ((f = fopen(oldfile, "r")) != NULL) {
	upstream = initfilelist(f, NULL, cmp_firstcolumn);
	fclose(f);
	if (upstream == NULL) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read %s", oldfile);
	    goto out;
	}
    } else {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m - new server?", oldfile);
	upstream = NULL;
    }

    /* read interesting.groups */
    if (!initinteresting()) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open interesting.groups");
	goto out;
    }
    /* add cmdline -N groups to r */
    add_fetchgroups();

    if ((r = openinteresting()) == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "no interesting.groups");
	goto out;
    }

    if ((f = fopen(newfile, "w")) == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "Could not open %s for writing: %m", newfile);
	goto out;
    }

    /* make newfile line buffered so it is somewhat up to date even if
     * fetchnews crashes hard. In case of error, log, but continue. */
    if (setvbuf(f, NULL, _IOLBF, 256))
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot set line buffer mode for %s: %m", newfile);

    while ((ng = readinteresting(r))) {
	struct newsgroup *g;
	unsigned long from;		/* old server high mark */
	unsigned long newserver = 0;	/* new server high mark */
	const char *donethisgroup;

	if (!isalnum((unsigned char)*ng))
	    continue;			/* FIXME: why? */

	from = get_old_watermark(ng, upstream);
	/* map our own error codes to 1, in case a buggy version of
	 * fetchnews wrote a bogus watermark that actually was an error
	 * code. XXX FIXME: we should return error out of band. */
	if (from == (unsigned long)-1
		|| from == (unsigned long)-2)
	    from = 1;

	donethisgroup = (const char *)rbfind(ng, done_groups);

	if (donethisgroup) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
		    "%s already fetched successfully, skipping", ng);
	}

	if (active && (!nglist || matchlist(nglist->head, ng)) && !donethisgroup) {
	    /* we still want this group */

	    g = findgroup(ng, active, -1);
	    if (!g) {
		if (!forceactive && (debugmode & DEBUG_ACTIVE))
		    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
			    "%s not found in groupinfo file", ng);
	    }

	    /* newserver == 0 means we'll be writing back the "from"
	     * article mark, to retry next run */
	    if (fault == 0)
		newserver = getgroup(cursrv, g, from);
	    else
		newserver = 0;
	    if (newserver == (unsigned long)-2) { /* "fatal" from getgroup */
		fault = 1;
		newserver = 0;
	    }
	    /* write back as good info as we have, drop if no real info */
	    if (newserver != 0) {
		fprintf(f, "%s %lu\n", ng, newserver);
	    } else if (from != 1) {
		fprintf(f, "%s %lu\n", ng, from);
	    }
	    /* remove from upstream tree */
	    remove_watermark(ng, upstream, from == 1);

	    if (newserver != 0) { /* group successfully fetched */
		if (only_fetch_once) {
		    char *k1 = critstrdup(ng, "processupstream");
		    const char *k2 = (const char *)rbsearch(k1, done_groups);
		    assert(k1 == k2);
		}
	    }

	} else {
	    if (from != 1)	/* write back old high mark if not fake */
		fprintf(f, "%s %lu\n", ng, from);
	}
    }
    if (log_fclose(f) == 0 &&
	log_rename(newfile, oldfile) == 0)
        rc = 1;
out:
    if (r)
	closeinteresting(r);
    freeinteresting();
    if (upstream)
	freegrouplist(upstream);

    free(newfile);
    free(oldfile);
    return fault ? 0 : rc;
}

/**
 * post all spooled articles to currently connected server
 * \return
 * -  1 if all postings succeed or there are no postings to post
 * -  0 if a posting is strange for some reason
 */
static int
postarticles(const struct serverlist *cursrv)
{
    char *line = 0;
    int n;
    unsigned long articles;
    char **x, **y;

    x = spooldirlist_prefix("out.going", DIRLIST_NONDOT, &articles);
    if (!x) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read out.going: %m");
	return 0;
    }

    ln_log(LNLOG_SINFO, LNLOG_CSERVER, "found %lu articles in out.going.",
	   articles);

    if (active == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "I need an active file (to figure which groups are moderated) before I can post.");
	free_dirlist(x);
	return 0;
    }

    n = 0;
    for (y = x; *y; y++) {
	FILE *f;
	if (!(f = fopen_reg(*y, "r"))) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "Cannot open %s to post, expecting regular file.", *y);
	} else {
	    struct stat st;
	    char *f1;

	    f1 = fgetheader(f, "Newsgroups:", 1);
	    if (0 == fstat(fileno(f), &st) && f1) {
		if (cursrv->post_anygroup || isgrouponserver(cursrv, f1)) {
		    char *f2;

		    f2 = fgetheader(f, "Message-ID:", 1);
		    if (f2) {
			if (ismsgidonserver(f2)) {
			    if (!(st.st_mode & S_IXUSR))
				ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
					"Message-ID of %s already in use upstream"
					" -- discarding article", *y);
			    if (unlink(*y)) {
				ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				       "Cannot delete article %s: %m", *y);
				/* FIXME: don't fail here */
			    }
			} else {
			    int xdup = 0;
			    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
				   "Posting %s", *y);
			    
			    if (post_FILE(cursrv, f, &line) || (xdup = strncmp(line, "441 435 ", 8) == 0)) {
				char *mod;
				char *app;

				if (xdup)
				    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
					    "Duplicate article %s, treating as success.", *y);

				/* don't post unapproved postings to
				 * moderated groups more than once
				 */
				mod = checkstatus(f1, 'm');
				app = fgetheader(f, "Approved:", 1);
				if (mod != NULL && app == NULL) {
				    (void)log_unlink(*y, 1);
				} else {
				    /* set u+x bit to mark article as posted */
				    chmod(*y, 0540);
				}

				if (mod != NULL)
				    free(mod);
				if (app != NULL)
				    free(app);

				/* POST was OK or duplicate */
				++n;
			    } else {
				/* POST failed */
				/* FIXME: TOCTOU race here - check for
				 * duplicate article here */

				ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
				       "Unable to post %s: \"%s\".", *y, line);
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
	   "%s: %d articles posted", cursrv->name, n);
    globalposted += n;
    return 1;
}


/**
 * works current_server.
 * \return
 * -  0 if no other servers have to be queried (for Message-ID fetch, e. g.)
 * -  1 if no errors, but more servers needed (normal return code)
 * - -1 for non fatal errors (go to next server)
 */
static int
do_server(struct serverlist *cursrv, int forceactive)
{
    char *e = NULL;
    int rc = -1;	/* assume non fatal errors */
    int res;
    int reply;
    int flag = 0;
    enum flags {
	f_mayshort = 1,
	f_mustnotshort = 2,
	f_error = 4
    };

    /* do not try to connect if we don't want to post here in -P mode */
    if (action_method == FETCH_POST
	    && cursrv -> feedtype != CPFT_NNTP) {
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "skipping %s:%hu - feedtype not NNTP",
		cursrv -> name, cursrv -> port);
	return 1;
    }

    /* establish connection to server */
    fflush(stdout);
    reply = nntpconnect(cursrv);
    if (reply == 0)
	return -1;	/* no connection */
    if (reply != 200 && reply != 201) {
	goto out;	/* unexpected answer, just quit */
    }

    /* authenticate */
    if (cursrv->username && !authenticate(cursrv)) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "%s@%s: error, cannot authenticate",
 	        cursrv->username, cursrv->name);
	goto out;
    }

    /* get the nnrpd on the phone */
    putaline(nntpout, "MODE READER");
    reply = newnntpreply(cursrv, &e);
    if (reply != 200 && reply != 201) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
		"%s: error: \"%s\"",
		cursrv->name, e != NULL ? e : "");
	goto out;
    }

    check_date(cursrv);

    /* get list of newsgroups or new newsgroups */
    if (!cursrv -> noactive) {
	if (nntpactive(cursrv, forceactive)) {
	    flag |= f_error;
	}
    } else {
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "%s: skipping newsgroups list update (noactive is set)",
		cursrv->name);
    }

    /* post articles */
    if (action_method & FETCH_POST) {
	flag |= f_mustnotshort;
	switch (cursrv->feedtype) {
	    case CPFT_NNTP:
		ln_log(LNLOG_SINFO, LNLOG_CSERVER,
			"%s: feedtype == NNTP.", cursrv->name);
		if (reply == 200) {
		    res = postarticles(cursrv);
		    if (res == 0 && rc >= 0)
			flag |= f_error;
		}
		break;
	    case CPFT_NONE:
		ln_log(LNLOG_SINFO, LNLOG_CSERVER,
			"%s: not posting, feedtype == none.", cursrv->name);
		break;
	    default:
		ln_log(LNLOG_SCRIT, LNLOG_CTOP,
			"fatal: %s: feedtype %s not implemented",
			cursrv->name, get_feedtype(cursrv->feedtype));
		exit(1);
	}
    }

    /* fetch by MID */
    switch (getmsgidlist(msgidlist)) {
    case 0:
	flag |= f_mayshort;
    default:
	;
    }

    /* do regular fetching of articles, headers, delayed bodies */
    if (action_method & (FETCH_ARTICLE|FETCH_HEADER|FETCH_BODY)
	    && !cursrv->noread) {
	res = processupstream(cursrv, cursrv->name,
		cursrv->port, forceactive);
	if (res != 1)
	    flag |= f_error;
    }

    if (flag & f_mustnotshort)
	flag &= ~f_mayshort;
    if (flag & f_error) {
	rc = -1;
    } else {
	rc = flag & f_mayshort ? 0 : 1;
    }

out:
    putaline(nntpout, "QUIT");	/* say it, then just exit :) */
    nntpdisconnect();
    return rc;
}

/* this is like sigaction, but it will not change a handler that is set
 * to SIG_IGN, and it does not allow queries. */
static int mysigaction(int signum, const struct sigaction *act)
{
    struct sigaction oa;
    sigaction(signum, NULL, &oa);
    if (oa.sa_handler != SIG_IGN) {
	return sigaction(signum, act, NULL);
    }
    return 0;
}

static mastr *activeread(void)
{
    mastr *c = mastr_new(100); /* FIXME? LN_PATH_MAX? */
    mastr_vcat(c, spooldir, "/leaf.node/:active.read", NULL);
    return c;
}

/* return 1 if forceactive is required, 0 if not */
static int checkactive(void)
{
    struct stat st;
    mastr *c = activeread();
    int passed, rc = 0;

    if (stat(mastr_str(c), &st)) {
	ln_log(errno == ENOENT ? LNLOG_SDEBUG : LNLOG_SERR, LNLOG_CTOP,
		"cannot stat %s: %s%s", mastr_str(c), strerror(errno),
		errno == ENOENT ? " - this message means the leafnode must redownload the active files." : "");
	mastr_delete(c);
	return 1;
    }

    passed = (now - st.st_mtime) / SECONDS_PER_DAY;
    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "last active refetch %d days ago.", passed);

    if (passed >= timeout_active)
	rc = 1;
    mastr_delete(c);
    return rc;
}

enum actmark { AM_UPDATE = 42, AM_KILL };

static int markactive(enum actmark am)
{
    int e;
    mastr *c = activeread();
    switch (am) {
	case AM_UPDATE:
	    if ((e = touch_truncate(mastr_str(c))) < 0)
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot touch or create %s: %m",
			mastr_str(c));
	    break;
	case AM_KILL:
	    e = log_unlink(mastr_str(c), 1);
	    break;
	default:
	    abort();
	    break;
    }
    mastr_delete(c);
    return e;
}

static void
error_refetch(const char *e) {
    ln_log(LNLOG_SERR, LNLOG_CTOP,
	    "ERROR: FETCHNEWS MUST REFETCH THE WHOLE ACTIVE FILE NEXT RUN.");
    ln_log(LNLOG_SERR, LNLOG_CTOP, "REASON: %s", e);
}

/**
 * main program
 */
int
main(int argc, char **argv)
{
    int reply;
    char *conffile = NULL;
    volatile int err = 2;
    volatile int rc = 0;
    volatile int postonly;
    volatile time_t starttime;	/* keep state across setjmp */
    static const char myname[] = "fetchnews";
    struct sigaction sa;
    static int forceactive;	/* if 1, reread complete active file */
    struct serverlist *current_server, *os;
#ifdef COMPILE_BROKEN
    unsigned long articles;
    char **x;
#endif

    forceactive = 0;
    verbose = 0;
    ln_log_open(myname);
    if (!initvars(argv[0], 0))
	init_failed(myname);

    starttime = time(NULL);
    now = time(NULL);

    if (process_options(argc, argv, &forceactive, &conffile) != 0)
	exit(EXIT_FAILURE);

    if ((reply = readconfig(conffile)) != 0) {
	printf("Reading configuration failed (%s).\n", strerror(reply));
	exit(2);
    }
    if (conffile)
	free(conffile);

    if (!init_post())
	init_failed(myname);

    if (!servers) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "%s: no servers found in configuration file.", myname);
	exit(EXIT_FAILURE);
    }

    if (filterfile && !readfilter(filterfile)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
		"%s: Cannot read filterfile %s, aborting.",
		myname, filterfile);
	exit(EXIT_FAILURE);
    }

    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	   "%s: version %s; verbosity level is %d; debugging level is %d",
	   myname, version, verbose, debugmode);
    if (noexpire) {
	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "%s: Not automatically unsubscribing unread newsgroups.",
	       myname);
    }

    /* pretty print intended action */
    if (action_method == 0 && msgidlist == NULL) {
	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "%s: Doing nothing.  Goodbye.\n", myname);
	exit(EXIT_SUCCESS);
    } else {
	print_fetchnews_mode(myname);
    }

    if ((action_method & ~FETCH_POST) == 0 && msgidlist == NULL)
	postonly = 1;
    else
	postonly = 0;

    if (!forceactive)
	forceactive |= checkactive();
    if (forceactive) {
	markactive(AM_KILL);
    }

    /* If fetchnews should post only, no lockfile or filters are required.
     * It is also sensible to check if there is anything to post when
     * invoked with -P; otherwise quit immediately.
     */
    if (postonly) {
	if (!checkforpostings()) {
	    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
		    "%s: no articles to post, exiting.", myname);
	    exit(EXIT_SUCCESS);
	}
    } else {
	if (attempt_lock(LOCKWAIT)) {
	    exit(EXIT_FAILURE);
	}

	if (!noexpire)
	    expireinteresting();

	if (only_fetch_once) {
	    done_groups = rbinit(cmp_firstcolumn, NULL);
	}
    }

    /* FIXME! Race condition here if no lockfile */
    rereadactive();
    feedincoming();
    if (forceactive) {
	oldactive = mvactive(active);
	active = NULL;
    }

    signal(SIGHUP, SIG_IGN);
    sa.sa_handler = sigcatch;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGALRM);
    if (mysigaction(SIGTERM, &sa))
	fprintf(stderr, "Cannot catch SIGTERM.\n");
    else if (mysigaction(SIGINT, &sa))
	fprintf(stderr, "Cannot catch SIGINT.\n");
    else if (mysigaction(SIGUSR1, &sa))
	fprintf(stderr, "Cannot catch SIGUSR1.\n");
    else if (mysigaction(SIGUSR2, &sa))
	fprintf(stderr, "Cannot catch SIGUSR2.\n");
    else if (sigsetjmp(jmpbuffer, 1) != 0) {
	servers = NULL;		/* in this case, jump the while ... loop */
	if (!rc) {
	    rc = 2;			/* and prevent writing "complete
					   markers" if we omit this, we
					   may never get rid of deleted
					   newsgroups */
	    if (forceactive)
		error_refetch("caught signal that caused a premature abort.");
	}
    } else {
	canjump = 1;
    }

    for (;servers; servers = servers->next) {
	/* time_t lastrun = 0;		/ * FIXME: save state for NEWNEWS */

	current_server = servers;

	for (os = only_server; os; os = os->next) {
	    if (0 ==  strcasecmp(current_server->name, os->name)) {
		current_server->port = os->port;
		err = do_server(current_server, forceactive);
		break;
	    }
	}
	if (!only_server)
	    err = do_server(current_server, forceactive);

	if (err == -2) {
	    abort(); /* -2 is undocumented for do_server! */
	    rc = 1;
	    break;
	}
	if (err == -1 && rc == 0) {
	    if (forceactive) {
		error_refetch("could not successfully talk to all servers.");
	    }
	    rc = 2;
	}
	if (err == 0) {
	    break;	/* no other servers have to be queried */
	}
    }
    if (err == 2) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "%s: No matching servers found.", myname);
    }

#ifdef COMPILE_BROKEN
    /* Check for unsent postings, provided we were able to talk to all
     * servers successfully. Do not touch articles that have been posted
     * before. */
    if (rc == 0 && action_method & FETCH_POST && !only_server) {
	char **y;
        x = spooldirlist_prefix("out.going", DIRLIST_NONDOT, &articles);
        if (!x) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read out.going: %m");
	} else {
	    y = x;
	    for (; *x; x++) {
		static const char xx[] = "/failed.postings/";
		struct stat st;

		/* successfully posted -> don't move to failed.postings */
		if (0 == lstat(*x, &st) && (S_IXUSR & st.st_mode))
		    continue;

		ln_log(LNLOG_SERR, LNLOG_CARTICLE,
			"Failed to post %s, moving to %s%s", *x,
			spooldir, xx);
		(void)log_moveto(*x, xx);
	    }
	    free_dirlist(y);
	}
    }
#endif

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);	/* FIXME */

    if (!postonly) {
	if (rc == 0 && forceactive) {
	    /* read local groups into the new active */
	    readlocalgroups();
	    mergeactives(oldactive, active);
	    freeactive(oldactive);
	}
	if (writeactive()) {
	    rc = 1;
	    error_refetch("error writing active file to disk.");
	    markactive(AM_KILL);
	}
	if (rc == 0 && forceactive)
	    markactive(AM_UPDATE);

	if (msgidlist && msgidlist->head->next) {
	    struct stringlistnode *n = msgidlist->head;
	    while(n->next) {
		ln_log(LNLOG_SNOTICE, LNLOG_CTOP, "%s: unfetched article %s", myname, n->string);
		n = n->next;
	    }
	}

	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "%s: %lu articles and %lu headers fetched, %lu killed, %lu posted, in %ld seconds",
	       myname, globalfetched, globalhdrfetched, globalkilled, globalposted, (long int)(time(0) - starttime));

	if (only_fetch_once)
	    freegrouplist(done_groups);

	unlink(lockfile);
    }

    if (only_server)
	freeservers(only_server);
    freeoptions();
    freexover();
    freeactive(active);
    active = NULL;
    freeallfilter(filter);
    freelocal();
    freeconfig();
    exit(rc);
}
