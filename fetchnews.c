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
static jmp_buf jmpbuffer;
struct serverlist *current_server;
time_t now;

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

struct stringlist *msgidlist = NULL;	/* list of Message-IDs to get (specify with -M) */
struct stringlist *nglist = NULL;	/* newsgroups patterns to fetch */

/* function declarations */
static void usage(void);
static int isgrouponserver(char *newsgroups);
static int ismsgidonserver(char *msgid);
static unsigned long getgroup(struct newsgroup *g, unsigned long server);
static int postarticles(void);
static int getarticle(/*@null@*/ struct filterlist *, /*@reldef@*/ unsigned long *, int);
static int getbymsgid(const char *msgid, int delayflg);

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

static RETSIGTYPE
sigcatch(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
	longjmp(jmpbuffer, signo);
    else if (signo == SIGUSR1)
	verbose++;
    else if (signo == SIGUSR2)
	verbose--;
}

static void deactivate_servers(void)
{
    struct serverlist *sl;

    sl = servers;
    while (sl) {
	sl->active = FALSE;
	sl = sl->next;
    }
}

static /*@null@*/ /*@dependent@*/ struct serverlist *
findserver(const char *servername)
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

/* add a new server before the other, using server:port if given */
static void
insert_server(char *servspec)
{
    struct serverlist *sl;
    char *p;

    if ((p = strchr(servspec, ':')) != NULL) {
	*p++ = '\0';
	sl = create_server(servspec, strtol(p, NULL, 10));
    } else {
	sl = create_server(servspec, 0);
    }
    sl->next = servers;
    servers = sl;
}

/* add the group names given on the cmdline to the interesting rbtree */
static void
add_fetchgroups(void)
{
    struct stringlist *sl = nglist;
    char *s;
    const char *t;

    while (sl) {
	/* do not add wildcards */
	if (!strpbrk(sl->string, "\\*?[")) {
	    s = critstrdup(sl->string, "add_fetchgroups");
	    t = addtointeresting(s);
	    if (t != NULL && t != s)	/* NULL: OOM, t!=s: repeated entry */
		free(s);
	}
	sl = sl->next;
    }
}

/**
 * parse fetchnews command line
 * \return
 * -  0 for success
 * - -1 for failure
 */
static int
process_options(int argc, char *argv[], int *forceactive)
{
    int option;
    char *p;
    struct serverlist *sl;
    struct stringlist *msgidlast = NULL;
    struct stringlist *nglast = NULL;

    /* state information */
    int action_method_seen = 0;	/* BHPR */
    int servers_limited = 0;

    while ((option = getopt(argc, argv, "VD:HBPRF:S:N:M:fnvx:p:t:")) != -1) {
	if (parseopt("fetchnews", option, optarg, NULL))
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
	    if (!servers_limited) {
		deactivate_servers();
		servers_limited = TRUE;
	    }
	    if ((sl = findserver(optarg)) != NULL)
		sl->active = TRUE;
	    else
		insert_server(optarg);
	    break;
	case 'N':
	    appendtolist(&nglist, &nglast, optarg);
	    break;
	case 'M':
	    if (!msgidlist) {
		*forceactive = 0;	/* don't load active */
	    }
	    if (!action_method_seen) {
		action_method_seen = TRUE;
		action_method = 0;	/* don't do anything else */
	    }
	    appendtolist(&msgidlist, &msgidlast, optarg);
	    break;
	case 'n':
	    noexpire = 1;
	    break;
	case 'f':
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
    ln_log(LNLOG_SINFO, LNLOG_CTOP, mastr_str(s));
    mastr_delete(s);
}

static void
usage(void)
{
    fprintf(stderr, "Usage:\n"
	    "fetchnews -V\n"
	    "    print version on stderr and exit\n"
	    "fetchnews [-BDfHnPRv] [-F configfile] [-t #] [-x #] [-S server]\n"
	    "          [-N newsgroup] [-N group.pattern] [-M message-id]\n"
	    "    -B                get article bodies in delaybody groups\n"
	    "    -D debugmode      switch on debugmode\n"
	    "    -f                force reload of groupinfo file\n"
	    "    -F conffile       use \"configfile\" instead of %s/config\n"
	    "    -H                get article headers in delaybody groups\n"
	    "    -n                switch off automatic unsubscription of groups\n"
	    "    -P                post only, don't get new articles\n"
	    "    -M message-id     get article by Message-ID\n"
	    "    -N newsgroup      get only articles in \"newsgroup\"\n"
	    "    -N group.pattern  get articles from all interesting groups\n"
	    "                      matching the wildcard \"group.pattern\"\n"
	    "    -R                get articles in non delaybody groups\n"
	    "    -S server         only get articles from \"server\"\n"
	    "    -t delay          wait \"delay\" seconds between articles\n"
	    "    -v                verbose mode (may be repeated)\n"
	    "    -x extra          go \"extra\" articles back in upstream history\n"
	    "Setting none of the options\n"
	    "    -B -H -P -R\n"
            "is equivalent to setting all of them, unless [-M message-id] is used.\n"
	    "Options [-S server], [-M message-id] and [-N newsgroup] may be repeated.\n"
	    "Articles specified by Message-ID will always be fetched as a whole,\n"
	    "no matter if they were posted to a delaybody group.\n"
	    "\n"
	    "See also the leafnode homepage at\n"
	    "    http://www.leafnode.org/\n", sysconfdir);
}

/**
 * check whether any of the newsgroups is on server
 * \return
 * - TRUE if yes
 * - FALSE otherwise
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
	SKIPLWS(p);
	q = strchr(p, ',');
	if (q)
	    *q++ = '\0';
	putaline(nntpout, "GROUP %s", p);
	if (nntpreply() == 211)
	    retval = TRUE;
	p = q;
    } while (q && !retval);
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
	l = getaline(nntpin);
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
    l = getaline(nntpin);
    if (l && get_long(l, &a) == 1 && a == 221) {
	_ignore_answer(nntpin);
	return TRUE;
    } else {
	return FALSE;
    }
}

/**
 * get a list of message-ids, remove successfully fetched ids
 * \return
 * -  0 if all articles successfully loaded
 * - -1 if articles outstanding
 * - -2 if server error, disconnect
 */
static int
getmsgidlist(struct stringlist **first)
{
    struct stringlist **slp = first;

    if (slp == NULL)	/* consistency check */
	return -1;
    if (*slp == NULL)	/* done! */
	return 0;

    ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
	   "Getting articles specified by Message-ID");
    groupfetched = 0;
    groupkilled = 0;

    /* remove MIDs of articles we already have
     * as well as successfully downloaded articles
     */
    while (*slp) {
	char *mid = (*slp)->string;

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
	*slp = (*slp)->next;
    }
    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
	    "%lu articles fetched by Message-ID, %lu killed",
	    groupfetched, groupkilled);
    globalfetched += groupfetched;
    globalkilled += groupkilled;
    return *first == NULL ? 0 : -1;
}


#if 0
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
	/* FIXME: if the upstream uses NNTPcache v2.4.0b5, it may send a
	 * line with a single dot after a 502 reply in violation of
	 * RFC-977 */
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
#endif

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
    mastr *fname = mastr_new(LN_PATH_MAX);
    mastr *str;

    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "Getting bodies of marked messages for group %s ...", group->name);
    (void)mastr_vcat(fname, spooldir, "/interesting.groups/", group->name,  NULL);
    if (!(f = fopen(mastr_str(fname), "r"))) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
	       "Cannot open %s for reading: %m", mastr_str(fname));
	mastr_delete(fname);
	return;
    }
    str = mastr_new(256);
    while ((l = getaline(f))) {
	char *p, *q, *sep;
	unsigned long artno;
	FILE *g;

	mastr_cpy(str, l);

	/* silently drop article with non existent pseudo head or bad formatted line */
	if (!(sep = strchr(mastr_str(str), ' ')))
	    continue;
	*sep = '\0';
	p = sep + 1;
	if (!*p)
	    continue;
	artno = strtoul(p, &q, 10);

	if (*q || !(g = fopen(p, "r")))
	    continue;
	fclose(g);

	if (!getbymsgid(mastr_str(str), 2)) {
	    *sep = ' ';			/* ugly, get original line back */
	    appendtolist(&failed, &ptr, mastr_str(str));
	}
    }
    fclose(f);
    mastr_delete(str);
    truncate(mastr_str(fname), (off_t) 0);	/* kill file contents */
    if (!failed) {
	mastr_delete(fname);
	return;
    }
    /* write back ids of all articles which could not be retrieved */
    if (!(f = fopen(mastr_str(fname), "w")))
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Cannot open %s for writing: %m", mastr_str(fname));
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
    mastr_delete(fname);
}

/**
 * calculate first and last article number to get
 * \return
 * -  0 for error
 * -  1 for success
 * - -1 if group is not available at all
 */
static int
getfirstlast(struct newsgroup *g, unsigned long *first, unsigned long *last,
    int delaybody_this_group)
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
	       "%s: backing up from %lu to %lu", g->name, h, *first);
	*first = h;
    }

    if (*first > *last + 1) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: last seen %s was %lu, server now has %lu - %lu",
	       g->name, delaybody_this_group ? "header" : "article",
	       *first, window, *last);
	if (*first > (*last + 5)) {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: switched upstream servers? %lu > %lu",
		   g->name, *first, *last);
	    *first = window;	/* check all upstream articles again */
	} else {
	    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		   "%s: rampant spam cancel? %lu > %lu",
		   g->name, *first - 1, *last);
	    *first = *last - 5;	/* re-check last five upstream articles */
	}
    }
    if (initiallimit && (*first == 1) && (*last - *first > initiallimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: skipping %s %lu-%lu inclusive (initial limit)",
	       g->name, delaybody_this_group ? "headers" : "articles",
	       *first, *last - initiallimit);
	*first = *last - initiallimit + 1;
    }
    if (artlimit && (*last + 1 - *first > artlimit)) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: skipping %s %lu-%lu inclusive (article limit)",
	       g->name, delaybody_this_group ? "headers" : "articles",
	       *first, *last - artlimit);
	*first = *last + 1 - artlimit;
    }
    if (window < *first)
	window = *first;
    if (window < 1)
	window = 1;
    *first = window;
    if (*first > *last) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP, "%s: no new %s", g->name,
	       delaybody_this_group ? "headers" : "articles");
	return 0;
    }
    return 1;
}


/** create pseudo article header from xover entries */
static /*@only@*/ mastr *
create_pseudo_header(const char *subject, const char *from,
	const char *date, const char *messageid, const char *references,
	char **newsgroups_list, int num_groups, const char *bytes, const char *lines)
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

    if (log_fsync(tmpfd) || log_close(tmpfd)) {
	rc = -1;
	goto out;
    }
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
doxover(struct stringlist **stufftoget,
	unsigned long first, unsigned long last,
	/*@null@*/ struct filterlist *filtlst, char *groupname)
{
    char *l;
    unsigned long count = 0;
    long reply;
    struct stringlist *helpptr = NULL;
    int delaybody_this_group = delaybody_group(groupname);

    putaline(nntpout, "XOVER %lu-%lu", first, last);
    l = getaline(nntpin);
    if (l == NULL || (!get_long(l, &reply)) || (reply != 224)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XOVER command: %s", l ? l : "(null)");
	return -2;
    }
    while ((l = getaline(nntpin)) && strcmp(l, ".")) {
	char *xover[20];	/* RATS: ignore */
	char *artno, *subject, *from, *date, *messageid;
	char *references, *lines, *bytes;
	char **newsgroups_list = NULL;
	int num_groups;

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

	/* is there an Xref: header present as well? */
	if (xover[8] == NULL ||
	    (num_groups = parsekill_xref_line(xover[8], &newsgroups_list, NULL, 0)) == -1) {
	    /* newsgroups filling by hand */
	    num_groups = 1;
	    newsgroups_list = critmalloc(num_groups * sizeof *newsgroups_list, "doxover");
	    newsgroups_list[0] = groupname;
	}

	if ((filtermode & FM_XOVER) || delaybody_this_group) {
	    mastr *s;

	    s = create_pseudo_header(subject, from, date, messageid,
		    references, newsgroups_list, num_groups, bytes, lines);

	    if (filtlst && killfilter(filtlst, mastr_str(s))) {
		groupkilled++;
		/* filter pseudoheaders */
		mastr_delete(s);
		goto next_over;
	    }
	    if (ihave(messageid)) {
		/* we have the article already */
		mastr_delete(s);
		goto next_over;
	    }

	    if (delaybody_this_group) {
		/* write pseudoarticle */
		if (store_pseudo_header(s) == 0)
		    count++;
	    } else {
		count++;
		appendtolist(stufftoget, &helpptr, artno);
	    }
	    mastr_delete(s);
	} else {
	    count++;
	    appendtolist(stufftoget, &helpptr, artno);
	}
next_over:
	free_strlist(xover);
	if (newsgroups_list)
	  free(newsgroups_list);
    }
    if (l && strcmp(l, ".") == 0)
	return count;
    else
	return -1;
}

/**
 * use XHDR to check which articles to get. This is faster than XOVER
 * since only message-IDs are transmitted, but you lose some features
 * \return
 * - -1 for error
 * - -2 if XHDR was rejected
 */
static long
doxhdr(struct stringlist **stufftoget, unsigned long first, unsigned long last)
{
    char *l;
    unsigned long count = 0;
    long reply;
    struct stringlist *helpptr = NULL;

    putaline(nntpout, "XHDR message-id %lu-%lu", first, last);
    l = getaline(nntpin);
    if (l == NULL || (!get_long(l, &reply)) || (reply != 221)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "Unknown reply to XHDR command: %s", l ? l : "(null)");
	return -2;
    }
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
    if (l && strcmp(l, ".") == 0)
	return count;
    else
	return -1;
}

/*
 * get articles
 */

/**
 * Get a single article and apply filters.
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
    int reply = 0;

    l = getaline(nntpin);
    if (!l) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "Server went away when it should send an article.");
	return -2;
    }

    if ((sscanf(l, "%3d %lu", &reply, artno) != 2) || (reply / 10 != 22)) {
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
	       "Wrong reply to ARTICLE command: %s", l);
	if (reply / 100 == 5)
	    return -2;		/* fatal error */
	return -1;
    }

    switch (store_stream(nntpin, 1, (filtermode & FM_HEAD ? filtlst : NULL),
			 -1, delayflg)) {
    case 1:
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
 */
static unsigned long
getarticles(/*@null@*/ struct stringlist *stufftoget, long n,
    /*@null@*/ struct filterlist *f)
{
    struct stringlist *p;
    long advance = 0, window;
    unsigned long artno_server = 0ul;
    long remain = sendbuf;

    p = stufftoget;
    window = (n < windowsize) ? n : windowsize;
    while (p || advance) {
	remain = sendbuf;
	/* stuff pipeline until TCP send buffer is full or window size
	 * is reached (preload, don't read anything) */
	while (p && advance < window && (advance == 0 || remain > 100)) {
	    const char *c;
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
		return 0;	/* disconnected server or store OS error */
	    /* FIXME: add timeout here and force window to 1 if it triggers */
	    if (p) {
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
    return artno_server;
}

/**
 * fetch all articles for that group.
 * \return
 * - 0 for error or if group is unavailable
 * - otherwise last article number in that group
 */
static unsigned long
getgroup(struct newsgroup *g, unsigned long first)
{
    struct stringlist *stufftoget = NULL;
    struct filterlist *f = NULL;
    int x = 0;
    long outstanding = 0;
    unsigned long last = 0;
    int delaybody_this_group;
    int tryxhdr = 0;

    /* lots of plausibility tests */
    if (!g)
	return first;
    if (!is_interesting(g->name))
	return 0;
    if (!chdirgroup(g->name, TRUE))	/* also creates the directory */
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
	g->last = g->first;

    x = getfirstlast(g, &first, &last, delaybody_this_group);

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
    if (!current_server->usexhdr || f || delaybody_this_group) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %ld %s %lu - %lu, using XOVER", g->name,
		(last - first + 1),
		delaybody_this_group ? "headers" : "articles",
		first, last);
	outstanding = doxover(&stufftoget, first, last, f, g->name);

	/* fall back to XHDR only without filtering or delaybody mode */
	if (outstanding == -2 && !f && !delaybody_this_group)
	    tryxhdr = 1;
    } else {
	tryxhdr = 1;
    }

    if (tryxhdr) {
	ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	       "%s: considering %ld articles %lu - %lu, using XHDR", g->name,
	       (last - first + 1), first, last);
	outstanding = doxhdr(&stufftoget, first, last);
    }

    switch (outstanding) {
    case -2:
	current_server->usexhdr = 0;	/* disable XHDR */
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
		   "%s: %lu pseudo headers fetched",
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
    if (getarticles(stufftoget, outstanding, f) == 0) {
	/* handle error */
    }
    ln_log(LNLOG_SNOTICE, LNLOG_CGROUP,
	   "%s: %lu articles fetched (to %lu), %lu killed",
	   g->name, groupfetched, g->last, groupkilled);
    globalfetched += groupfetched;
    globalkilled += groupkilled;

    freefilter(f);
    freelist(stufftoget);
    return last + 1;
}

/** Split a line which is assumed in RFC-977 LIST format.  Puts a
 * pointer to the end of the newsgroup name into nameend (the caller
 * should then set this to '\0'), and a pointer to the status character
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
 * get active file from current_server
 */
static void
nntpactive(int fa)
{
    struct stat st;
    char *l, *p, *q;
    struct stringlist *groups = NULL;
    struct stringlist *helpptr = NULL;
    struct newsgroup *oldactive;
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
    p = server_info(spooldir, current_server->name, current_server->port, "");
    if (0 == stat(p, &st))
	last_update = st.st_mtime;
    else
	/* never fetched before */
	forceact = 1;
    free(p);

    str_ulong(portstr, current_server->port);
    mastr_vcat(s, spooldir, "/leaf.node/last:", current_server->name, ":", portstr, NULL);

    if (!forceact && (0 == stat(mastr_str(s), &st))) {
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
		"%s: checking for new newsgroups", current_server->name);
	/* "%Y" and "timestr + 2" avoid Y2k compiler warnings */
	strftime(timestr, 64, "%Y%m%d %H%M%S", gmtime(&last_update));
	putaline(nntpout, "NEWGROUPS %s GMT", timestr + 2);
	if (nntpreply() != 231) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER, "Reading new newsgroups failed");
	    mastr_delete(s);
	    return;
	}
	while ((l = getaline(nntpin)) && (strcmp(l, ".") != 0)) {
	    char *r;
	    count++;
	    p = l;
	    if (!splitLISTline(l, &p, &r))
		continue;
	    *p = '\0';
	    insertgroup(l, *r, 0, 0, time(NULL), NULL);
	    appendtolist(&groups, &helpptr, l);
	}
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
		"%s: found %lu new newsgroups", current_server->name, count);
	if (!l) {
	    /* timeout */
	    mastr_delete(s);
	    return;
	}
	mergegroups();		/* merge groups into active */
	helpptr = groups;
	if (count && current_server->descriptions) {
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		    "%s: getting new newsgroup descriptions",
		    current_server->name);
	}
	while (count && helpptr != NULL && current_server->descriptions) {
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
    } else {    /* read new active */
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		"%s: getting newsgroups list", current_server->name);
	putaline(nntpout, "LIST");
	if (nntpreply() != 215) {
	    ln_log(LNLOG_SERR, LNLOG_CSERVER,
		    "%s: reading all newsgroups failed", current_server->name);
	    mastr_delete(s);
	    return;
	}
	oldactive = cpactive(active);
	while ((l = getaline(nntpin)) && (strcmp(l, "."))) {
	    last = first = 0;
	    count++;
	    p = l;
	    if (!splitLISTline(l, &q, &p))
		continue;
	    *q = '\0';		/* cut out the group name */

	    /* see if the newsgroup is interesting.  if it is, and we
	       don't have it in groupinfo, figure water marks */
	    /* FIXME: save high water mark in .last.posting? */
	    if (is_interesting(l)
		    && (forceact || !(active && findgroup(l, active, -1)))
		    && chdirgroup(l, FALSE)) {
		first = ULONG_MAX;
		last = 0;
		if (getwatermarks(&first, &last, 0)) {
		    /* trouble */
		    first = last = 0;
		}
	    }
	    insertgroup(l, p[0], first, last, 0, NULL);
	}
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		"%s: read %lu newsgroups", current_server->name, count);

	mergegroups();
	/*		if (!l)
			timeout 
			mastr_delete(namelast);
			return; */

	if (current_server->descriptions) {
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
		    free(oldactive);
		    mastr_delete(s);
		    return;
		}
	    } else {
		ln_log(LNLOG_SERR, LNLOG_CSERVER,
			"%s: reading newsgroups descriptions failed",
			current_server->name);
		free(oldactive);
		mastr_delete(s);
		return;
	    }
	    while (l && (strcmp(l, "."))) {
		p = l;
		CUTSKIPWORD(p);
		changegroupdesc(l, *p ? p : NULL);
		l = getaline(nntpin);
	    }
	    if (!l) {
		free(oldactive);
		mastr_delete(s);
		return;		/* timeout */
	    }
	}
	mergeactives(oldactive, active);
	free(oldactive); /* Do not call freeactive(). The pointers in 
			    oldactive will be free()d by freeactive(active)
			    at the end. */
	/* touch file */
	{
	    int e = touch_truncate(mastr_str(s));
	    if (e < 0)
		ln_log(LNLOG_SERR, LNLOG_CGROUP, "cannot touch %s: %m", mastr_str(s));
	}
	mastr_delete(s);
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

/**
 * post all spooled articles to currently connected server
 * \return
 * -  1 if all postings succeed or there are no postings to post
 * -  0 if a posting is strange for some reason
 */
int
postarticles(void)
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

    n = 0;
    for (y = x; *y; y++) {
	FILE *f;
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
				    (void)log_moveto(*y, "/backup.moderated/");
				++n;
			    } else {
				/* POST failed */
				static const char xx[] = "/failed.postings/";

				ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				       "Unable to post %s: \"%s\", "
				       "moving to %s%s", *y, line,
				       spooldir, xx);
				(void)log_moveto(*y, xx);
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
    globalposted += n;
    return 1;
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

    k = rbfind(group, upstream);

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
    k  = rbdelete(group, upstream);
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
processupstream(const char *const server, const unsigned short port,
		int forceactive)
{
    FILE *f;
    const char *ng;			/* current group name */
    char *newfile, *oldfile;		/* temp/permanent server info files */
    RBLIST *r = NULL;			/* interesting groups pointer */
    struct rbtree *upstream;		/* upstream water marks */
    int rc = 0;				/* return value */

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

    while ((ng = readinteresting(r))) {
	struct newsgroup *g;
	unsigned long from;		/* old server high mark */
	unsigned long newserver = 0;	/* new server high mark */
	const char *donethisgroup;

	if (!isalnum((unsigned char)*ng))
	    continue;			/* FIXME: why? */

	from = get_old_watermark(ng, upstream);

	donethisgroup = rbfind(ng, done_groups);

	if (donethisgroup) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
		    "%s already fetched successfully, skipping", ng);
	}

	if ((!nglist || matchlist(nglist, ng)) && !donethisgroup) {
	    /* we still want this group */

	    g = findgroup(ng, active, -1);
            if (!g) {
                if (!forceactive && (debugmode & DEBUG_ACTIVE))
                    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
                            "%s not found in groupinfo file", ng);
            }

	    newserver = getgroup(g, from);
	    if (newserver == (unsigned long)-2) { /* fatal */
		/* FIXME: write back all other upstream entries, return */
		goto out;
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
		    const char *k2 = rbsearch(k1, done_groups);
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
    return rc;
}


/**
 * works current_server.
 * \return
 * -  0 if no other servers have to be queried,
 * -  1 if no errors, but more servers needed
 * - -1 for non fatal errors (go to next server)
 * - -2 for fatal errors (do not query any more)
 */
static int
do_server(int forceactive)
{
    char *e = NULL;
    int rc = -1;	/* assume non fatal errors */
    int res;
    int reply;

    /* establish connection to server */
    fflush(stdout);
    reply = nntpconnect(current_server);
    if (reply == 0)
	return -1;	/* no connection */
    if (reply != 200 && reply != 201) {
	goto out;	/* unexpected answer, just quit */
    }

    /* authenticate */
    if (current_server->username && !authenticate()) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "%s: error, cannot authenticate",
		current_server->name);
	goto out;
    }

    /* get the nnrpd on the phone */
    putaline(nntpout, "MODE READER");
    reply = newnntpreply(&e);
    if (reply != 200 && reply != 201) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
		"%s: error: \"%s\"",
		current_server->name, e != NULL ? e : "");
	goto out;
    }

    rc = 1;	/* now assume success */

    /* fetch by MID */
    switch (getmsgidlist(&msgidlist)) {
    case 0:
	rc = 0;
    default:
	;
    }

    /* do regular fetching of articles, headers, delayed bodies */
    if (action_method & (FETCH_ARTICLE|FETCH_HEADER|FETCH_BODY)) {
	nntpactive(forceactive);	/* get list of newsgroups or new newsgroups */
	res = processupstream(current_server->name, current_server->port,
			forceactive);
	if (res == 1)
	    rc = 1;
	else
	    rc = -1;
    }

    /* post articles */
    if (action_method & FETCH_POST) {
	switch(current_server->feedtype) {
	    case CPFT_NNTP:
		if (reply == 200) {
		    res = postarticles();
		    if (res == 0 && rc >= 0)
			rc = -1;
		    else if (rc == 0)
			rc = 1;		/* when posting, query all servers! */
		}
		break;
	    case CPFT_NONE:
		break;
	    default:
		ln_log(LNLOG_SCRIT, LNLOG_CTOP,
			"fatal: feedtype %s not implemented",
			get_feedtype(current_server->feedtype));
		exit(1);
	}
    }

out:
    putaline(nntpout, "QUIT");	/* say it, then just exit :) */
    nntpdisconnect();
    return rc;
}

/* this is like sigaction, but it will not change a handler that is set
 * to SIG_IGN, and it does not allow queries. */
static int mysigaction(int signum, const  struct  sigaction  *act) {
    struct sigaction oa;
    sigaction(signum, NULL, &oa);
    if (oa.sa_handler != SIG_IGN) {
	return sigaction(signum, act, NULL);
    }
    return 0;
}

static mastr *activeread(void)
{
    mastr *c = mastr_new(100);
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
		"cannot stat %s: %m%s", mastr_str(c),
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

/**
 * main program
 */
int
main(int argc, char **argv)
{
    int reply;
    char *t, *conffile = NULL;
    volatile int err;
    volatile int rc = 0;
    volatile int postonly;
    volatile time_t starttime;	/* keep state across setjmp */
    static const char myname[] = "fetchnews";
    struct sigaction sa;
    int forceactive = 0;	/* if 1, reread complete active file */

    verbose = 0;
    ln_log_open("fetchnews");
    if (!initvars(myname, 0))
	exit(EXIT_FAILURE);

    starttime = time(NULL);
    now = time(NULL);
    umask(2);

    if ((t = getoptarg('F', argc, argv)) != NULL)
	conffile = critstrdup(t, myname);

    if ((reply = readconfig(conffile)) != 0) {
	printf("Reading configuration failed (%s).\n", strerror(reply));
	exit(2);
    }
    if (conffile)
	free(conffile);

    if (process_options(argc, argv, &forceactive) != 0)
	exit(EXIT_FAILURE);

    if (!servers) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "%s: no servers found in configuration file.", myname);
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
    if (forceactive)
	markactive(AM_KILL);
    rereadactive();

    feedincoming();

    /* If fetchnews should post only, no lockfile or filters are required.
     * It is also sensible to check if there is anything to post when
     * invoked with -P; otherwise quit immediately.
     */
    if (postonly) {
	if (!checkforpostings())
	    exit(EXIT_SUCCESS);
    } else {
	if (lockfile_exists(LOCKWAIT)) {
	    fprintf(stderr, "%s: lockfile %s exists, abort\n",
		    myname, lockfile);
	    exit(EXIT_FAILURE);
	}

	if (filterfile && !readfilter(filterfile)) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "%s: Cannot read filterfile %s, aborting.",
		   myname, filterfile);
	    exit(EXIT_FAILURE);
	}

	if (!noexpire)
	    expireinteresting();

	if (only_fetch_once) {
	    done_groups = rbinit(cmp_firstcolumn, NULL);
	}
    }
    signal(SIGHUP, SIG_IGN);
    sa.sa_handler = sigcatch;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&sa.sa_mask);
    if (mysigaction(SIGTERM, &sa))
	fprintf(stderr, "Cannot catch SIGTERM.\n");
    else if (mysigaction(SIGINT, &sa))
	fprintf(stderr, "Cannot catch SIGINT.\n");
    else if (mysigaction(SIGUSR1, &sa))
	fprintf(stderr, "Cannot catch SIGUSR1.\n");
    else if (mysigaction(SIGUSR2, &sa))
	fprintf(stderr, "Cannot catch SIGUSR2.\n");
    else if (setjmp(jmpbuffer) != 0) {
	servers = NULL;		/* in this case, jump the while ... loop */
    }

    while (servers) {
	/* time_t lastrun = 0;		/ * FIXME: save state for NEWNEWS */

	current_server = servers;
	if (current_server->active) {
	    err = do_server(forceactive);
	    if (err == -2) {
		rc = 1;
		break; 
	    }
	    if (err == -1 && rc == 0)
		rc = 2;
	    if (err == 0) {
		break;	/* no other servers have to be queried */
	    }
	}
	servers = servers->next;
    }

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);	/* FIXME */

    if (!postonly) {
	writeactive();
	if (rc == 0)
	    markactive(AM_UPDATE);

	ln_log(LNLOG_SINFO, LNLOG_CTOP,
	       "%s: %lu articles and %lu headers fetched, %lu killed, %lu posted, in %ld seconds",
	       myname, globalfetched, globalhdrfetched, globalkilled, globalposted, time(0) - starttime);

	if (only_fetch_once)
	    freegrouplist(done_groups);

	switch (fork()) {
	case -1:		/* problem */
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "%s: fork: %m, running on parent schedule", myname);
	    fixxover();
	    unlink(lockfile);
	    break;
	case 0:			/* child */
	    setsid();
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "%s: Fixing XOVER", myname);
	    fixxover();
	    unlink(lockfile);
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
		   "%s: Background process finished", myname);
	    _exit(0);
	default:		/* parent */
	    ;
	}
    }
    wait(0);
    freeoptions();
    freexover();
    freeactive(active);
    active = NULL;
    freeallfilter(filter);
    freelocal();
    freeconfig();
    exit(rc);
}
