/*
  nntpd -- the NNTP server
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "validatefqdn.h"

#include <unistd.h>
#include <assert.h>

#include "critmem.h"
#include "ln_log.h"
#include "get.h"
#include "attributes.h"
#include "mastring.h"
#include "h_error.h"
#include "masock.h"
#include "msgid.h"
#include "mailto.h"

/* FIXME: write wrapper for this fellow */
#ifdef SOCKS
#include <socks.h>
#endif

#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
/* FIXME: is arpa/inet.h still needed after masock_* split? */
#ifndef __LCLINT__
#include <arpa/inet.h>
#endif /* not __LCLINT__ */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <utime.h>

#ifdef USE_PAM
#ifdef HAVE_SECURITY_PAM_APPL_H
#include <security/pam_appl.h>
#endif
#endif

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define MAXLINELENGTH 1000

/* for authorization: */
#define P_NOT_SUPPORTED	503
#define P_SYNTAX_ERROR	501
#define P_REJECTED	482
#define P_CONTINUE	381
#define P_ACCEPTED	281

static /*@null@*/ /*@only@*/ char *NOPOSTING;
				/* ok-to-print version of getenv("NOPOSTING") */
static /*@dependent@*/ const struct newsgroup *xovergroup;	/* FIXME */
/*@null@*/ static struct stringlisthead *users = NULL;	/* FIXME */
				/* users allowed to use the server */
static int authflag = 0;	/* TRUE if authenticated */
static char *peeraddr = NULL;	/* peer address, for X-NNTP-Posting header */

/*
 * this function avoids the continuous calls to both ln_log and printf
 * it also appends \r\n automagically
 * slightly modified from a function nntp_log_and_reply() written by
 * Matthias Andree <ma@dt.e-technik.uni-dortmund.de> (c) 1999
 */
static void nntpprintf(const char *fmt, ...)
    __attribute__ ((format(printf, 1, 2)));
static void
nntpprintf(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (debugmode & DEBUG_NNTP)
	ln_log(LNLOG_SDEBUG, LNLOG_CALL, ">%s", buffer);
    printf("%s\r\n", buffer);
    (void)fflush(stdout);
    va_end(args);
}

/* same as nntpprintf_as, but don't flush output */
static void nntpprintf_as(const char *fmt, ...)
    __attribute__ ((format(printf, 1, 2)));
static void
nntpprintf_as(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (debugmode & DEBUG_NNTP)
	ln_log(LNLOG_SDEBUG, LNLOG_CALL, ">%s", buffer);
    printf("%s\r\n", buffer);
    va_end(args);
}

/*
 * pseudo article stuff
 */
static void
fprintpseudobody(FILE * pseudoart, const char *groupname)
{
    FILE *f;
    char *l, *cl, *c;

    if (pseudofile && ((f = fopen(pseudofile, "r")) != NULL)) {
	while ((l = getaline(f)) != NULL) {
	    cl = l;
	    while ((c = strchr(cl, '%')) != NULL) {
		if (strncmp(c, "%%", 2) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%%", cl);
		    cl = c + 2;
		} else if (strncmp(c, "%server", 7) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, owndn ? owndn : fqdn);
		    cl = c + 7;
		} else if (strncmp(c, "%version", 8) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, version);
		    cl = c + 8;
		} else if (strncmp(c, "%newsgroup", 10) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, groupname);
		    cl = c + 10;
		} else {
		    *c = '\0';
		    fprintf(pseudoart, "%s%%", cl);
		    cl = c + 1;
		}
	    }
	    fprintf(pseudoart, "%s\n", cl);
	}
	(void)fclose(f);
    } else {
	if (pseudofile)
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "Unable to read pseudoarticle from %s: %m", pseudofile);
	/* fall back to English standard text */
	fprintf(pseudoart,
		"This server is running leafnode, which is a dynamic NNTP proxy.\n"
		"This means that it does not retrieve newsgroups unless someone is\n"
		"actively reading them.\n"
		"\n"
		"If you do an operation on a group - such as reading an article,\n"
		"looking at the group table of contents or similar, then leafnode\n"
		"will go and fetch articles from that group when it next updates.\n"
		"\n");
	fprintf(pseudoart,
		"Since you have read this dummy article, leafnode will retrieve\n"
		"the newsgroup %s when fetchnews is run\n"
		"the next time. If you'll look into this group a little later, you\n"
		"will see real articles.\n"
		"\n", groupname);
	fprintf(pseudoart,
		"If you see articles in groups you do not read, that is almost\n"
		"always because of cross-posting.  These articles do not occupy any\n"
		"more space - they are hard-linked into each newsgroup directory.\n"
		"\n"
		"If you do not understand this, please talk to your newsmaster.\n"
		"\n"
		"Leafnode can be found at http://leafnode.sourceforge.net/\n\n");
    }
}


/** build and return an open FILE stream to pseudoart in group, may
 * return NULL. */
/*@null@*/ /*@dependent@*/ static FILE *
buildpseudoart(const char *grp)
{
    FILE *f;

    f = tmpfile();
    if (!f) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP,
	       "Could not create pseudoarticle for group %s", grp);
	return f;
    }
    ln_log(LNLOG_SINFO, LNLOG_CGROUP,
	   "Creating pseudoarticle for group %s", grp);
    fprintf(f, "Path: %s!not-for-mail\n", owndn ? owndn : fqdn);
    fprintf(f, "Newsgroups: %s\n", grp);
    fprintf(f, "From: Leafnode <news@%s>\n", owndn ? owndn : fqdn);
    fprintf(f, "Subject: Leafnode placeholder for group %s\n", grp);
    fprintf(f, "Date: %s\n", rfctime());
    fprintf(f, "Message-ID: <leafnode:placeholder:%s@%s>\n", grp,
	    owndn ? owndn : fqdn);
    fprintf(f, "\n");
    fprintpseudobody(f, grp);
    rewind(f);
    return f;
}

/* open a pseudo art */
/*@null@*/ /*@dependent@*/ static FILE *
fopenpseudoart(const struct newsgroup *group, const char *arg,
	       const unsigned long article_num)
{
    FILE *f = NULL;
    char msgidbuf[128];
    char *c;
    struct newsgroup *g;

    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	   "fopenpseudoart %s: first %lu last %lu artno %lu",
	   group->name, group->first, group->last, article_num);
    if (article_num && article_num == group->first) {
	f = buildpseudoart(group->name);
    } else if (!article_num) {
	if (!strncmp(arg, "<leafnode:placeholder:", 22)) {
	    strncpy(msgidbuf, arg + 22, 128);
	    msgidbuf[127] = '\0';
	    if ((c = strchr(msgidbuf, '@')) != NULL) {
		*c = '\0';
		g = findgroup(msgidbuf, active, -1);
		if (g)
		    f = buildpseudoart(g->name);
	    }
	}
    }
    return f;
}

static int
allowsubscribe(void)
{
    char *s;
    static int allowsubs; /* 0: uninitialized, 1: can subscribe,
			     2: cannot subscribe */
    if (!allowsubs) {
	s = getenv("NOSUBSCRIBE");
	allowsubs = (s != 0 ? 2 : 1);
    }
    return 2 - allowsubs;
}

/* return FALSE if group is NOT a pseudo group 
 * return TRUE  if group is a pseudo group 
 */
static int
is_pseudogroup(const char *group)
{
    struct newsgroup *g;

    /* warning: the order of this checks is relevant */

    /* local is never pseudo */
    if (is_localgroup(group)) return FALSE;

    /* when the client cannot subscribe, no group will be pseudo */
    if (!allowsubscribe()) return FALSE;

    /* interesting is never pseudo */
    if (is_interesting(group)) return FALSE;

    /* make sure that is_localgroup is FALSE here: */
    if (!chdirgroup(group, FALSE)) return TRUE;

    /* group not in active -> not pseudo either */
    if ((g = findgroup(group, active, -1)) == NULL) return FALSE;

    /* empty group -> pseudo */
    if (g->count == 0ul) return TRUE;

    /* non-empty group -> not pseudo */
    return FALSE;
}

/* note bug.. need not be _immediately after_ GROUP */
/* returns 0 for success, errno for error */
static int
markinterest(const char *group)
{
    struct stat st;
    struct utimbuf buf;
    int not_yet_interesting;
    time_t now;
    FILE *f;
    mastr *s;

    if (is_localgroup(group))
	return 0;		/* local groups don't have to be marked */
    if (is_dormant(group))
	return 0;		/* dormant groups don't have to be marked */

    not_yet_interesting = 0;

    s = mastr_new(LN_PATH_MAX);
    mastr_vcat(s, spooldir, "/interesting.groups/", group, NULL);

    if (stat(mastr_str(s), &st) == 0) {
	/* already marked interesting, update atime */
	now = time(0);
	buf.actime = (now < st.st_atime) ? st.st_atime : now;
	buf.modtime = st.st_mtime;
	/* NOTE: as a side effect, the ctime is also set to now, so effectively, we don't need to care for  */
	if (utime(mastr_str(s), &buf)) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "Cannot update timestamp on %s: %m", mastr_str(s));
	    not_yet_interesting = 1;
	}
    } else {
	not_yet_interesting = 1;
    }

    if (not_yet_interesting && allowsubscribe()) {
	f = fopen(mastr_str(s), "w");
	if (f) {
	    if (fclose(f)) {
		int e = errno;
		ln_log(LNLOG_SERR, LNLOG_CGROUP,
		       "Could not write to %s: %m", mastr_str(s));
		mastr_delete(s);
		return e;
	    } else {
		mastr_delete(s);
		return 0;
	    }
	} else {
	    int e = errno;
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "Could not create %s: %m",
		   mastr_str(s));
	    mastr_delete(s);
	    return e;
	}
    }
    mastr_delete(s);
    return 0;
}

/* open an article by number or message-id */
/*@null@*/ /*@dependent@*/ static FILE *
fopenart(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long *artno)
/* FIXME: when may artno be touched? */
{
    unsigned long int a;
    FILE *f;
    char *t;
    struct stat st;

    f = NULL;
    t = NULL;
    a = strtoul(arg, &t, 10);
    if (a && t && !*t && group != NULL) {
	if (is_pseudogroup(group->name)) {
	    f = fopenpseudoart(group, arg, a);
	} else {
	    f = fopen(arg, "r");
	}
	if (!f && errno != ENOENT)
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "cannot open %s in %s: %m", arg, group->name);

	if (f)
	    *artno = a;

	markinterest(group->name);	/* FIXME: check error */
	/* else try message-id lookup */
    } else if (arg && *arg == '<') {
	if (0 == strncmp(arg, "<leafnode:placeholder:", 22) && allowsubscribe())
	{
	    f = fopenpseudoart(group, arg, 0);
	} else {
	    f = fopen(lookup(arg), "r");
	}
	if (!f && errno != ENOENT)
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "cannot open %s: %m", arg);
    } else if (group && *artno) {
	char s[64];
	(void)chdirgroup(group->name, FALSE);
	sprintf(s, "%lu", *artno);
	f = fopen(s, "r");
	if (!f && is_pseudogroup(group->name) && allowsubscribe())
	    f = fopenpseudoart(group, s, *artno);
	if (!f && errno != ENOENT)
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "cannot open %s in %s: %m", arg, group->name);
	/* warning: f may be NULL */
	markinterest(group->name);	/* FIXME: check error */
    }

    /* do not return articles with zero size (these have been truncated by
     * store.c after a write error or by applyfilter to get deleted) */
    if (f && (fstat(fileno(f), &st) || st.st_size == 0)) {
	(void)fclose(f);
	f = NULL;
    }

    return f;
}

/*
 * parse Xref line to find an article number to mark for download
 * give preference to current newsgroup if any
 * f will not be at same position afterwards
 * \return null for failure or malloced markgroup name, store markartno then
 */
static /*@null@*/ /*@only@*/ char *
getmarkgroup(/*@null@*/ const char *groupname, FILE *f,
	     /*@out@*/ unsigned long *markartno)
{
    char *xref, *p;
    char *markgroup = NULL;
    unsigned long a = 0;	/* squish compiler warning */
    char **ngs, **artnos;
    int n, num_groups;

    *markartno = 0;
    xref = fgetheader(f, "Xref:", 1);
    if (!xref)
	return NULL;

    if ((num_groups = xref_to_list(xref, &ngs, &artnos, 1)) == -1) {
	free(xref);
	return NULL;
    }

    p = NULL;
    if (groupname != NULL && delaybody_group(groupname)) {
	for (n = 0; n < num_groups; n++) {
	    if (!strcmp(groupname, ngs[n])) {
		a = strtoul(artnos[n], NULL, 10);
		p = ngs[n];
		break;
	    }
	}
    }

    if (p == NULL) {
	for (n = 0; n < num_groups; n++) {
	    if (is_interesting(ngs[n]) && delaybody_group(ngs[n])) {
		a = strtoul(artnos[n], NULL, 10);
		p = ngs[n];
		break;
	    }
	}
    }
    if (p != NULL) {
	*markartno = a;
	markgroup = critstrdup(p, "getmarkgroup");
    }

    free(xref);
    free(ngs);
    free(artnos);

    return markgroup;
}


/*
 * Mark an article for download by appending its number to the
 * corresponding file in interesting.groups
 */
static int
markdownload(const char *groupname, const char *msgid, unsigned long artno)
{
    int e = 0;
    char *l;
    FILE *f;
    mastr *s;

    if (!groupname)
	return -1;
    s = mastr_new(LN_PATH_MAX);
    mastr_vcat(s, spooldir, "/interesting.groups/", groupname, NULL);
    if ((f = fopen(mastr_str(s), "r+"))) {
	while ((l = getaline(f)) != NULL) {
	    if (strncmp(l, msgid, strlen(msgid)) == 0) {
		(void)fclose(f);
		mastr_delete(s);
		return 0;	/* already marked */
	    }
	    if (ferror(f)) e = errno;
	}
	(void)fprintf(f, "%s %lu %lu\n", msgid, artno, (unsigned long)time(NULL));
	if (ferror(f)) e = errno;
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	       "Marking %s: %s for download", groupname, msgid);
	if (fclose(f)) e = errno;
    }
    if (e) {
	ln_log(LNLOG_SERR, LNLOG_CGROUP, "I/O error handling \"%s\": %s",
	       mastr_str(s), strerror(e));
	mastr_delete(s);
	return -1;
    }
    mastr_delete(s);
    return 1;
}

static int
allowposting(void)
    /*@globals undef NOPOSTING@*/
{
    char *s;
    static int allowpost = 0; /* 0: uninitialized, 1: can post,
				 2: cannot post */

    /* read NOPOSTING from environment if true */
    if (!allowpost) {
	s = getenv("NOPOSTING");
	if (s) {
	    char *p;
	    NOPOSTING = critstrdup(s, "allowposting");
	    for (p = NOPOSTING; *p; p++) {
		if (iscntrl((unsigned char)*p))
		    *p = '_';
	    }
	    allowpost = 2;
	} else {
	    NOPOSTING = NULL;
	    allowpost = 1;
	}
    }
    return 2 - allowpost;
}

/* display an article or somesuch */
/* DOARTICLE */
static void
doarticle(/*@null@*/ const struct newsgroup *group, const char *arg, int what,
	  unsigned long *artno)
/* what & 1: show body
   what & 2: show header */
{
    FILE *f;
    unsigned long localartno;
    char *localmsgid = NULL;
    char *l;
    static const char *whatyouget[] = {
	"request text separately",
	"body follows",
	"head follows",
	"text follows"
    };

    assert(what >= 0 && what <= 3);

    f = fopenart(group, arg, artno);
    if (!f) {
	if (arg && *arg != '<' && !group)
	    nntpprintf("412 No newsgroup selected");
	else if (*arg)
	    nntpprintf("430 No such article: %s", arg);
	else
	    nntpprintf("423 No such article: %lu", *artno);
	return;
    }

    if (!*arg) {
	localartno = *artno;
	localmsgid = fgetheader(f, "Message-ID:", 1);
    } else if (*arg == '<') {
	localartno = 0;
	localmsgid = critstrdup(arg, "doarticle");
    } else {
	localartno = strtoul(arg, NULL, 10);
	localmsgid = fgetheader(f, "Message-ID:", 1);
    }

    nntpprintf_as("%3d %lu %s article retrieved - %s", 223 - what,
	    localartno, localmsgid, whatyouget[what]);

    while ((l = getaline(f)) && *l) {
	if (what & 2) {
	    if (*l == '.')
		putc('.', stdout);	/* escape . */
	    fputs(l, stdout);
	    fputs("\r\n", stdout);
	}
    }

    if (what == 3)
	fputs("\r\n", stdout);		/* empty separator line */

    if (what & 1) {
	/*
	 * in delaybody mode, we check for the separator line between
	 * header and body. If this line is present, the body is also
	 * present. If the blank line is missing, the body will also be
	 * missing.
	 */
	/* EOF -> no body */
	if (!l) {
	    unsigned long markartno;
	    char *markgroup;

	    /* parse Xref to get (markgroup, markartno) here */
	    markgroup = getmarkgroup(group ? group->name : NULL, f, &markartno);
	    switch (markdownload(markgroup, localmsgid, markartno)) {
	    case 0:
	    case 1:
		printf("\r\n\r\n"
			"\t[Leafnode:]\r\n"
			"\t[Message %lu of %s]\r\n"
			"\t[has been marked for download.]\r\n",
			markartno, markgroup);
		break;
	    default:
		/* XXX FIXME: is the text correct? */
		printf("\r\n\r\n"
			"\t[ Leafnode: ]\r\n"
			"\t[ Body of message %s ]\r\n"
			"\t[ is empty but cannot be marked for download. ]\r\n"
			"\t[ Apparently none of the interesting groups ]\r\n"
			"\t[ this message was posted to is in delaybody mode ]\r\n",
			localmsgid);
	    }
	    if (markgroup)
		free(markgroup);
	} else {
	    while ((l = getaline(f))) {
		if (*l == '.')
		  putc('.', stdout);	/* escape . */
		fputs(l, stdout);
		fputs("\r\n", stdout);
	    }
	}
    }

    if (what)
	fputs(".\r\n", stdout);

    (void)fclose(f);

    free(localmsgid);
    return;			/* FIXME: OF COURSE there were no errors */
}

static struct newsgroup *
opengroup(struct newsgroup *g)
{
    freexover();
    xovergroup = NULL;
    /* If this group is interesting and requested, update the time stamp
       so it remains interesting even without articles */
    if (is_interesting(g->name))
	markinterest(g->name);
    if (chdirgroup(g->name, FALSE)) {
	xgetxover(0, g, 0);
#if 0
	if (g->count == 0) {
	    if (getwatermarks(&g->first, &g->last, &g->count)) {
		nntpprintf("503 Cannot get group article count.");
		return NULL;
	    }
	}
#endif
    }
    return g;
}



/* change to group - note no checks on group name */
static /*@null@*/ /*@dependent@*/ struct newsgroup *
dogroup(struct newsgroup *group, const char *arg, unsigned long *artno)
{
    struct newsgroup *g;

    rereadactive();
    g = findgroup(arg, active, -1);
    if (g) {
	opengroup(g);

	if (is_pseudogroup(g->name))
	    nntpprintf("211 %lu %lu %lu %s group selected",
		    1lu, g->first, g->first, g->name);
	else
	    nntpprintf("211 %lu %lu %lu %s group selected",
		    g->count, min(g->last, g->first), g->last, g->name);
	*artno = g->first;

	return g;
    } else {
	nntpprintf("411 No such group");
	return group;
    }
}

static void
dohelp(void)
{
    printf("100 Legal commands\r\n");
/*  printf("  authinfo user Name|pass Password|generic <prog> <args>\r\n"); */
    printf("  authinfo user Name|pass Password\r\n");
    printf("  article [MessageID|Number]\r\n");
    printf("  body [MessageID|Number]\r\n");
    printf("  date\r\n");
    printf("  group newsgroup\r\n");
    printf("  hdr header [range|MessageID]\r\n");
    printf("  head [MessageID|Number]\r\n");
    printf("  help\r\n");
/*  printf("  ihave\r\n"); */
    printf("  last\r\n");
/*  printf("  list [active|newsgroups|distributions|schema] [group_pattern]\r\n"); */
    printf("  list [active|newsgroups] [group_pattern]\r\n");
    printf("  list [extensions|overview.fmt]\r\n");
    printf("  listgroup newsgroup\r\n");
    printf("  mode reader\r\n");
    printf("  newgroups yymmdd hhmmss [\"GMT\"] [<distributions>]\r\n");
    printf
	("  newnews newsgroups yymmdd hhmmss [\"GMT\"] [<distributions>]\r\n");
    printf("  next\r\n");
    printf("  over [range]\r\n");
    printf("  pat header range|MessageID pat [morepat...]\r\n");
    if (allowposting())
	printf("  post\r\n");
    printf("  quit\r\n");
    printf("  slave\r\n");
    printf("  stat [MessageID|Number]\r\n");
/*  printf("  xgtitle [group_pattern]\r\n"); */
    printf("  xhdr header [range|MessageID]\r\n");
    printf("  xover [range]\r\n");
    printf("  xpat header range|MessageID pat [morepat...]\r\n");
/*  printf("  xpath MessageID\r\n"); */
    printf(".\r\n");
}

static void
domode(const char *arg)
{
    if (!strcasecmp(arg, "reader"))
	nntpprintf("%03d Leafnode %s, pleased to meet you!",
		   201 - allowposting(), version);
    else
	nntpprintf("500 MODE other than READER not supported");
}

static void
domove(/*@null@*/ const struct newsgroup *group, int by, unsigned long *artno)
{
    char *msgid;
    char s[64];
    unsigned long a;

    by = (by < 0) ? -1 : 1;
    if (group) {
	if (*artno) {
	    a = *artno;
	    a += by;
	    do {
		sprintf(s, "%lu", a);
		msgid = getheader(s, "Message-ID:");
		if (!msgid)
		    a += by;
	    } while (!msgid && a >= group->first && a <= group->last);
	    if (a > group->last) {
		nntpprintf("421 There is no next article");
	    } else if (a < group->first) {
		nntpprintf("422 There is no previous article");
	    } else {
		*artno = a;
		nntpprintf("223 %lu %s article retrieved", *artno, msgid);
	    }
	    if (msgid)
		free(msgid);
	} else {
	    nntpprintf("420 There is no current article");
	}
    } else {
	nntpprintf("412 No newsgroup selected");
    }
}

static int is_pattern(const char *s) {
    return s ? strcspn(s, "?*[") < strlen(s) : 1;
}

/* LIST ACTIVE if what==0, else LIST NEWSGROUPS */
static void printlist(const struct newsgroup *ng, const int what) {
    if (what) {
	printf("%s\t%s", ng->name, ng->desc ? ng->desc : "-x-");
	if (ng->status == 'm' && (!ng->desc || !strstr(ng->desc, " (Moderated)")))
	    printf(" (Moderated)");
	printf("\r\n");
    } else {
	printf("%s %010lu %010lu %c\r\n", ng->name, ng->last,
		ng->first, ng->status);
    }
}

/* LIST ACTIVE if what==0, else LIST NEWSGROUPS */
static void
list(struct newsgroup *g, int what, char *pattern)
{
    struct newsgroup *ng;
    char *p;

    /* if pattern exists, convert to lowercase */
    if (pattern) {
	p = pattern;
	while (*p) {
	    *p = tolower((unsigned char)*p);
	    p++;
	}
    }

    if (!is_pattern(pattern)) {
	/* not a pattern - accelerate and refresh subscription */
	ng = findgroup(pattern, active, -1);
	if (ng) {
	    printlist(ng, what);
	    if (what == 0 && is_interesting(pattern))
		markinterest(pattern);
	}
    } else {
	/* have a pattern */
	ng = g;
	while (ng->name) {
	    if (!pattern || !ngmatch(pattern, ng->name)) {
		printlist(ng, what);
	    }
	    ng++;
	}
    }
}

static void
dolist(char *oarg)
{
    char *const arg = critstrdup(oarg, "dolist");

    if (!strcasecmp(arg, "extensions")) {
	nntpprintf_as("202 extensions supported follow");
	fputs("HDR\r\n" "OVER\r\n" "XPAT\r\n" "LISTGROUP\r\n", stdout);
	if (authentication)
	    fputs(" AUTHINFO USER\r\n", stdout);
	fputs(".\r\n", stdout);
    } else if (!strcasecmp(arg, "overview.fmt")) {
	nntpprintf_as("215 information follows");
	fputs("Subject:\r\n"
	       "From:\r\n"
	       "Date:\r\n"
	       "Message-ID:\r\n"
	       "References:\r\n"
	       "Bytes:\r\n" "Lines:\r\n" "Xref:full\r\n" ".\r\n", stdout);
    } else if (!strcasecmp(arg, "active.times")) {
#if 1
	fputs("500 not implemented\r\n", stdout);
#else
	nntpprintf_as("215 Placeholder - Leafnode will fetch groups on demand");
	fputs("news.announce.newusers 42 tale@uunet.uu.net\r\n"
	       "news.answers 42 tale@uunet.uu.net\r\n" ".\r\n", stdout);
#endif
    } else {
	rereadactive();
	if (!active) {
	    ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		      "503 Group information file does not exist!");
	} else if (!*arg || str_isprefix(arg, "active")) {
	    nntpprintf_as("215 Newsgroups in form \"group high low flags\".");
	    if (active) {
		if (!*arg || strlen(arg) == 6)
		    list(active, 0, NULL);
		else {
		    char *p = arg;
		    SKIPWORD(p);
		    list(active, 0, p);
		}
	    }
	    fputs(".\r\n", stdout);
	} else if (str_isprefix(arg, "newsgroups")) {
	    nntpprintf_as("215 Descriptions in form \"group description\".");
	    if (active) {
		if (strlen(arg) == 10)
		    list(active, 1, NULL);
		else {
		    char *p = arg;
		    SKIPWORD(p);
		    list(active, 1, p);
		}
	    }
	    fputs(".\r\n", stdout);
	} else {
	    nntpprintf("503 Syntax error");
	}
    }
    free(arg);
}

static void
dodate(void)
{
  time_t now = time(0);
  struct tm *t = gmtime(&now);
  char timestr[64];
  strftime(timestr, sizeof timestr, "%Y%m%d%H%M%S", t);
  nntpprintf("111 %s", timestr);
}

static time_t
parsedate_newnews(const char *date_str, const char *time_str, const int gmt)
{
    struct tm timearray;
    time_t age;
    long a, b;

    memset(&timearray, 0, sizeof(timearray));
    a = strtol(date_str, NULL, 10);
    /* NEWNEWS/NEWGROUPS dates may have the form YYMMDD or YYYYMMDD.
     * Distinguish between the two */
    b = a / 10000;
    if (b < 100) {
	/* YYMMDD */
	if (b < 70)
	    timearray.tm_year = b + 100;
	else
	    timearray.tm_year = b;
    } else if (b < 1000) {
	/* YYYMMDD - happens with buggy newsreaders */
	/* In these readers, YYY=100 is equivalent to YY=00 or YYYY=2000 */
	ln_log(LNLOG_SNOTICE, LNLOG_CSERVER,
	       "NEWGROUPS year is %ld: please update your newsreader", b);
	timearray.tm_year = b;
    } else {
	/* [Y]YYYYMMDD -- we don't check for years > 2037 */
	timearray.tm_year = b - 1900;
    }
    timearray.tm_mon = (a % 10000 / 100) - 1;
    timearray.tm_mday = a % 100;
    a = strtol(time_str, NULL, 10);
    timearray.tm_hour = a / 10000;
    timearray.tm_min = a % 10000 / 100;
    timearray.tm_sec = a % 100;
    /* mktime() shall guess correct value of tm_isdst(0 or 1) */
    timearray.tm_isdst = -1;
    /* mktime() assumes local time -> correct according to timezone
       (global variable set by last call to localtime()) if GMT is
       not requested */
    age = mktime(&timearray);
    if (age != (time_t)-1 && gmt) {
	time_t off = gmtoff(age);
	age += off;
    }
    return age;
}

static time_t
donew_common(const struct stringlisthead *l, int newnews)
{
    struct stringlistnode *n;
    int newnews_len;
    int gmt, len;
    time_t age;

    newnews_len = 2;
    if (newnews) newnews_len++;
    if (!l || (len = stringlistlen(l)) < newnews_len) {
	nntpprintf("502 Syntax error");
	return -1;
    }

    n = l->head;
    if (newnews) n = n->next;

    gmt = (len >= (newnews_len+1) && !strcasecmp(n->next->next->string, "gmt"));

    age = parsedate_newnews(n->string, n->next->string, gmt);
    if (age == (time_t)-1) {
	nntpprintf("502 Syntax error");
	return -1;
    }

    rereadactive();

    return age;
}

static void
donewnews(char *arg)
{
    struct stringlisthead *l = cmdlinetolist(arg);
    struct stat st;
    time_t age;
    DIR *d, *ng;
    struct dirent *de, *nga;
    mastr *s;

    if (!l) {
	nntpprintf("502 Syntax error.");
	return;
    }
    age = donew_common(l, 1);
    if (age == (time_t)-1) {
	freelist(l);
	return;
    }

    nntpprintf_as("230 List of new articles since %ld in newsgroup %s",
		    (long int)age, l->head->string);
    s = mastr_new(LN_PATH_MAX);
    mastr_vcat(s, spooldir, "/interesting.groups", NULL);
    d = opendir(mastr_str(s));
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Unable to open directory %s: %m",
	       mastr_str(s));
	fputs(".\r\n", stdout);
	freelist(l);
	mastr_delete(s);
	return;
    }
    if (!strpbrk(l->head->string, "\\*?[")) 
	markinterest(l->head->string);
    while ((de = readdir(d))) {
	if (ngmatch(l->head->string, de->d_name) == 0) {
	    chdirgroup(de->d_name, FALSE);
	    xgetxover(1, NULL, 0);
	    ng = opendir(".");
	    while ((nga = readdir(ng))) {
		unsigned long artno;

		if (get_ulong(nga->d_name, &artno)) {
		    if ((stat(nga->d_name, &st) == 0) &&
			(*nga->d_name != '.') && S_ISREG(st.st_mode) &&
			(st.st_ctime > age)) {
			long xo = findxover(artno);

			if (xo >= 0) {
			    char *x = getxoverfield(xoverinfo[xo].text, XO_MESSAGEID);
			    if (x) {
				fputs(x, stdout);
				fputs("\r\n", stdout);
				free(x);
			    } else {
				/* FIXME: cannot find message ID in XOVER */
				ln_log(LNLOG_SERR, LNLOG_CTOP,
					"Cannot find Message-ID in XOVER for %s",
					nga->d_name);
			    }
			} else {
			    /* FIXME: cannot find XOVER for article */
			    ln_log(LNLOG_SERR, LNLOG_CTOP,
				    "Cannot find XOVER record for %s", nga->d_name);
			}
		    }		/* too old */
		}		/* if not a number, not an article */
	    }			/* readdir loop */
	    closedir(ng);
	    freexover();
	}
    }
    xovergroup = NULL;
    closedir(d);
    freelist(l);
    mastr_delete(s);
    fputs(".\r\n", stdout);
}

static void
donewgroups(const char *arg)
{
    struct stringlisthead *l = cmdlinetolist(arg);
    time_t age;
    struct newsgroup *ng;

    age = donew_common(l, 0);
    if (age == (time_t)-1) {
	freelist(l);
	return;
    }

    nntpprintf_as("231 List of new newsgroups since %lu follows",
	       (unsigned long)age);
    ng = active;
    while (ng->name) {
	if (ng->age >= age)
	    printf("%s %lu %lu %c\r\n", ng->name, ng->last, ng->first, ng->status);
	ng++;
    }
    fputs(".\r\n", stdout);
    freelist(l);
}

/* next bit is copied from INN 1.4 and modified("broken") by agulbra
   mail to Rich $alz <rsalz@uunet.uu.net> bounced */
/* Scale time back a bit, for shorter Message-ID's. */
#define OFFSET	(time_t)1026380000L
static char ALPHABET[] = "0123456789abcdefghijklmnopqrstuv";
static char *
generateMessageID(void)
{
    static char buff[80];
    static time_t then;
    static unsigned int fudge;
    time_t now;
    char *p;
    unsigned long n;

    now = time(0);		/* might be 0, in which case fudge 
				   will almost fix it */
    if (now < OFFSET) {
	ln_log(LNLOG_SCRIT, LNLOG_CTOP,
		"your system clock cannot be right. abort.");
	abort();
    }

    if (now != then)
	fudge = 0;
    else
	fudge++;

    then = now;
    p = buff;
    *p++ = '<';
    n = (unsigned int)now - OFFSET;
    while (n) {
	*p++ = ALPHABET[(int)(n & 31)];
	n >>= 5;
    }
    *p++ = 'x';
    n = fudge * 32768 + (int)getpid();
    while (n) {
	*p++ = ALPHABET[(int)(n & 31)];
	n >>= 5;
    }
    sprintf(p, ".ln2@%s>", owndn ? owndn : fqdn);
    return (critstrdup(buff, "generateMessageID"));
}

/* the end of what I stole from rsalz and then mangled */

/* checks for malformatted newsgroups header */
/* returns 0 if malformatted (spurious whitespace) */
static int
validate_newsgroups(const char *n)
{
    const char *t;

    if (str_isprefix(n, "Newsgroups:")) {
	n += 11;
	/* skip separating whitespace */
	n += strspn(n, WHITESPACE);
    }
    if (!*n)
	return 0;	/* do not tolerate empty Newsgroups: header */

    /* find other whitespace */
    t = strpbrk(n, WHITESPACE);
    if (t) {
#if 0
	/* tolerant version */
	/* we have found whitespace, check if it's just trailing */
	if (strspn(t, WHITESPACE) != strlen(t))
	    return 0;
#else
	/* pedantic version */
	return 0;
#endif
    }
    return 1;
}

/** Check if the Message-ID is acceptable, feed with n pointing to the
 * first angle bracket. This version is pedantic: no whitespace,
 * no weird characters, no bogus domains.
 */
static int
validate_messageid(const char *n)
{
    const char *m = n;
    const char *r;
    char *s;
    size_t l;

    /* must begin with '<' */
    if (*n != '<')
	return 0;
    /* must end with '>', no control characters or whitespace allowed */
    while (*++n != '\0' && *n != '>') {
	if (isspace((unsigned char)*n) || iscntrl((unsigned char)*n))
	    return 0;
    }
    /* must end with '>' */
    if (*n++ != '>')
	return 0;
    /* but tolerate trailing white space after angle bracket */
    SKIPLWS(n);
    /* barf if nonspace character */
    if (*n != '\0')
	return 0;
    /* check for valid domain part */
    r = strrchr(m, (int)'@');
    /* barf if no at ('@') present */
    if (!r) return 0;
    r++;
    l = strcspn(r, ">");
    s = (char *)critmalloc(l+1, "validate_messageid");
    *s = '\0';
    strncat(s, r, l);
    l = is_validfqdn(s);
    free(s);
    return l;
}


static void
dopost(void)
{
    char *line, *msgid;
    int havefrom = 0;
    int havepath = 0;
    int havedate = 0;
    int havebody = 0;
    int havenewsgroups = 0;
    int havemessageid = 0;
    int havesubject = 0;
    int err = 0;
    int hdrtoolong = FALSE;
    size_t len;
    int out;
    char inname[LN_PATH_MAX];
    static int postingno;	/* starts at 0 */

    do {
	sprintf(inname, "%s/temp.files/%d-%lu-%d",
		spooldir, (int)getpid(), (unsigned long)time(NULL),
		++postingno);
	out = open(inname, O_WRONLY | O_EXCL | O_CREAT, (mode_t) 0440);
    } while (out < 0 && errno == EEXIST);

    if (out < 0) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "441 Unable to open spool file %s: %m", inname);
	return;
    }

    msgid = generateMessageID();
    nntpprintf("340 Ok, recommended ID %s", msgid);
    /* get headers */
    do {
	line = getaline(stdin);

	if (!line) {
	    /* client died */
	    log_unlink(inname, 0);
	    ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
		    "Client disconnected while POSTing headers. Exit.");
	    exit(0);
	}

	/* premature end (no body) */
	if (0 == strcmp(line, ".")) {
	    err = TRUE;
	    break;
	}

	if (str_isprefix(line, "From:")) {
	    if (havefrom)
		err = TRUE;
	    else
		havefrom = TRUE;
	}

	if (str_isprefix(line, "Path:")) {
	    if (havepath)
		err = TRUE;
	    else
		havepath = TRUE;
	}

	if (str_isprefix(line, "Message-ID:")) {
	    if (havemessageid)
		err = TRUE;
	    else {
		havemessageid = TRUE;
	    }
	}

	if (str_isprefix(line, "Subject:")) {
	    if (havesubject)
		err = TRUE;
	    else
		havesubject = TRUE;
	}

	if (str_isprefix(line, "Newsgroups:")) {
	    if (havenewsgroups)
		err = TRUE;
	    else {
		char *p = line + 11;
		SKIPLWS(p);
		if (!*p) {	/* disallow folded Newsgroups: header */
		    err = TRUE;
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "folding not allowed in Newsgroups: header in %s",
			   inname);
		}
		havenewsgroups = TRUE;
	    }
	}

	if (str_isprefix(line, "Date:")) {
	    if (havedate)
		err = TRUE;
	    else
		havedate = TRUE;
	}

	if (ln_log_posterip
		&& str_isprefix(line, "X-Leafnode-NNTP-Posting-Host:"))
	    continue;

	len = strlen(line);
	/* maximal header length of 1000 chars is suggested in RFC.
	   Some people have requested that leafnode should barf if
	   headers are too long. If you don't want this, just remove
	   the following lines.
	 */

	if (len > 1000) {
	    hdrtoolong = TRUE;
	    err = TRUE;
	}
	if (len) {
	    /* regular header line, write it */
	    write(out, line, len);
	} else {
	    /* separator between header and body, defer writing to
	     * append missing headers */
	    if (!havepath) {
		writes(out, "Path: ");
		if (owndn)
		    writes(out, owndn);
		else
		    writes(out, fqdn);
		writes(out, "!not-for-mail\r\n");
	    }
	    if (!havedate) {
		const char *l = rfctime();

		writes(out, "Date: ");
		writes(out, l);
		writes(out, "\r\n");
	    }
	    if (!havemessageid) {
		writes(out, "Message-ID: ");
		writes(out, msgid);
		writes(out, "\r\n");
	    }
	    /* Build the X-Leafnode-NNTP-Posting-Host header if required */
	    if (ln_log_posterip) {
		writes(out, "X-Leafnode-NNTP-Posting-Host: ");
		writes(out, peeraddr ? peeraddr : "(unknown)");
		writes(out, "\r\n");
	    }
	}
	writes(out, "\r\n");
    } while (*line);

    free(msgid);
    /* get bodies */
    if (strcmp(line, ".")) {	/* skip if header contained a single dot line */
	havebody = TRUE;
	for (;;) {
	    line = getaline(stdin);

	    if (!line) {
		/* client died */
		log_unlink(inname, 0);
		ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
			"Client disconnected while POSTing body. Exit.");
		exit(0);
	    }

	    if (line[0] == '.') {
		/* escape or skip if single dot */
		if (line[1]) {
		    writes(out, line + 1);
		} else {
		    break;	/* end of article */
		}
	    } else {
		writes(out, line);
	    }
	    writes(out, "\r\n");
	}
	/* safely write to disk before reporting success */
	if (log_fsync(out))
	    err = 1;
	if (log_close(out))
	    err = 1;
    }

    if (havefrom && havesubject && havenewsgroups && !err) {
	FILE *f;
	char *mid = 0;
	char *groups = 0;
	char *outbasename;
	char *forbidden = 0;
	char *modgroup = 0;
	char *moderator = 0;
	char *approved = 0;
	char *legalgroup = 0;
	mastr *outgoingname = NULL;
	mastr *incomingname = NULL;

	f = fopen(inname, "r");
	if (f) {
	    mid = fgetheader(f, "Message-ID:", 0);
	    groups = fgetheader(f, "Newsgroups:", 1);
	    log_fclose(f);
	}

	if (!f || !mid || !groups) {
	    /* heck, cannot read the file just written */
	    if (!f)
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "cannot open %s for reading: %m", inname);
	    else
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "cannot read message-id or newsgroups from %s", inname);
	    nntpprintf("503 Could not reopen article, see syslog");
	    log_unlink(inname, 0);
	    return;
	}

	if (!validate_newsgroups(groups))
	{
	    nntpprintf
		("441 Spurious whitespace in Newsgroups: header, fix your news reader");
	    log_unlink(inname, 0);
	    goto cleanup;
	}

	if (havemessageid) {
	    if (!validate_messageid(mid)) {
		nntpprintf("441 Invalid header \"Message-ID: %s\", article not posted", mid);
		log_unlink(inname, 0);
		goto cleanup;
	    }
	}

	if (filter) {
	    char *t, *u = critstrdup(groups, "dopost");
	    int fd;
	    char *l;
	    size_t lsize = MAXHEADERSIZE + 1;
	    l = (char *)critmalloc(lsize, "Space for article");

	    /* read header */
	    fd = open(inname, O_RDONLY);
	    if (fd < 0 || readheaders(fd, inname, &l, &lsize, "\r\n\r\n") < 0) {
		nntpprintf("441 internal error.");
		free(u);
		log_unlink(inname, 0);
		goto cleanup;
	    }

	    /* apply filter for all newsgroups found in turn */
	    for (t = strtok(u, ", "); t; t = strtok(NULL, ", ")) {
		struct filterlist *fi = selectfilter(t);
		if (killfilter(fi, l)) {
		    nntpprintf("441 Article rejected by filter.");
		    log_unlink(inname, 0);
		    free(u);
		    goto cleanup;
		}
	    }
	    free(u);
	}

	/* check if we can obtain the MID or if the article is duplicate */
	switch(msgid_allocate(inname, mid)) {
	    case 1:
		nntpprintf("441 435 Duplicate, article not posted.");
		log_unlink(inname, 0);
		goto cleanup;
	    case 0:
		/* OK */
		break;
	    default:
		nntpprintf("441 Server error: cannot allocate Message-ID.");
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot link %s to %s: %m",
			inname, lookup(mid));
		log_unlink(inname, 0);
		goto cleanup;
	}

	if ((forbidden = checkstatus(groups, 'n'))) {
	    /* Newsgroups: contains a group to which posting is not allowed */
	    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		   "Article was posted to non-writable group %s", forbidden);
	    nntpprintf("441 Posting to %s not allowed", forbidden);
	    free(forbidden);
	    goto unlink_cleanup;
	}

	/* set up all that we need */
	legalgroup = checkstatus(groups, 'y');
	modgroup = checkstatus(groups, 'm');
	if (modgroup)
	    moderator = getmoderator(modgroup);
	approved = getheader(inname, "Approved:");
	outbasename = strrchr(inname, '/');
	outbasename++;
	outgoingname = mastr_new(LN_PATH_MAX);
	incomingname = mastr_new(LN_PATH_MAX);
	mastr_vcat(outgoingname, spooldir, "/out.going/", outbasename, NULL);
	mastr_vcat(incomingname, spooldir, "/in.coming/", outbasename, NULL);

	ln_log(LNLOG_SINFO, LNLOG_CTOP, "%s POST %s %s",
		modgroup && !approved
		? "MODERATED" 
		: ( is_anylocal(groups)
		    ? ( is_alllocal(groups)
			? "LOCAL" 
			: "LOCAL+UPSTREAM")
		    : "UPSTREAM"), mid, groups);

	if (!modgroup && !legalgroup) {
	    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		   "Article was posted to unknown groups %s", groups);
	    nntpprintf("441 No such newsgroups %s.", groups);
	    goto unlink_cleanup;
	}

	if (modgroup && !approved) {
	    if (moderator) {
		int fd = open(inname, O_RDONLY);
		if (fd < 0) {
		    ln_log(LNLOG_SERR, LNLOG_CTOP, "Couldn't open article %s: %m",
			    inname);
		    nntpprintf("503 file open error caught at %s:%lu", __FILE__,
			    (unsigned long)__LINE__);
		} else {
		    int mrc = mailto(moderator, fd);
		    if (mrc) {
			ln_log(LNLOG_SERR, LNLOG_CARTICLE,
				"message %s, ID <%s>: mailing to moderator <%s> failed (error #%d)", inname,
				mid, moderator, mrc);
			nntpprintf("503 posting to moderator <%s> failed: %m", moderator);
		    } else {
			ln_log(LNLOG_SINFO, LNLOG_CARTICLE,
				"message %s, ID <%s> mailed to moderator <%s>", inname,
				mid, moderator);
			nntpprintf("240 Article mailed to moderator <%s>", moderator);
		    }
		    (void)close(fd);
		}
		goto unlink_cleanup;
	    } else if (is_alllocal(groups)) {
		nntpprintf("503 Configuration error: no moderator for group %s.", modgroup);
		goto unlink_cleanup;
	    }
	}

	if (!is_alllocal(groups)) {
	    if (sync_link(inname, mastr_str(outgoingname))) {
		ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		       "unable to schedule for posting to upstream, "
		       "link %s -> %s failed: %m", inname, mastr_str(outgoingname));
		nntpprintf("503 Could not schedule article for posting "
			   "to upstream, see syslog.");
		goto unlink_cleanup;
	    }
	}

	if (!(modgroup && !approved) && /* don't store unapproved moderated posts */
		(0 == no_direct_spool /* may spool directly */ || is_anylocal(groups))) {
	    /* at least one internal group is given, store into in.coming */
	    if (sync_link(inname, mastr_str(incomingname))) {
		ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		       "unable to schedule for posting to local spool, "
		       "link %s -> %s failed: %m", inname, mastr_str(incomingname));
		nntpprintf("503 Could not schedule article for posting "
			   "to local spool, see syslog.");
		/* error with spooling locally -> also drop from out.going (to avoid
		 * that the user resends the article after the 503 code)
		 */
		if (!is_alllocal(groups))
			log_unlink(mastr_str(outgoingname), 0);
		goto unlink_cleanup;
	    }
	} else {
	    /* remove message.id link so fetchnews can download the
	     * posting */
	    log_unlink(lookup(mid), 0);
	}
	log_unlink(inname, 0);

	switch (fork()) {
	case -1:
	    /* parent (fork failed) */
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "dopost: fork: %m, "
		   "local post will show up delayed.");
	    nntpprintf("240 Posting accepted, but local post will show up "
		       "delayed, see syslog.");
	    break;

	case 0:
	    /* child */
	    fclose(stdin);
	    fclose(stdout);
	    fclose(stderr);

	    if (attempt_lock(2UL)) {
		ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		       "Cannot obtain lock file to store article %s. "
		       "It will be posted later.", inname);
		_exit(EXIT_FAILURE);
	    }

	    rereadactive();
	    feedincoming();
	    writeactive();
	    log_unlink(lockfile, 0);
	    _exit(0);

	default:
	    nntpprintf("240 Article posted, now be patient");
	    break;
	}			/* switch(fork()) */

	goto cleanup;

unlink_cleanup:
	msgid_deallocate(inname, mid);
cleanup:
	if (mid) free(mid);
	if (groups) free(groups);
	if (modgroup) free(modgroup);
	if (legalgroup) free(legalgroup);
	if (moderator) free(moderator);
	if (approved) free(approved);
	if (outgoingname) mastr_delete(outgoingname);
	if (incomingname) mastr_delete(incomingname);
	return;
    } /* if (havefrom && havesubject && havenewsgroups && !err) */

    log_unlink(inname, 0);
    if (!havefrom)
	nntpprintf("441 From: header missing, article not posted");
    else if (!havesubject)
	nntpprintf("441 Subject: header missing, article not posted");
    else if (!havenewsgroups)
	nntpprintf("441 Newsgroups: header missing, article not posted");
    else if (hdrtoolong)
	nntpprintf("441 Header too long, article not posted");
    else if (!havebody)
	nntpprintf("441 Article has no body, not posted");
    else
	nntpprintf("441 Error, article not posted, see syslog.");

}

/** call this only if a previous parserange with the same arg yielded no
 * error return!
 *
 * \return
 *  - 1 for success
 *  - 0 if no articles are found (this is printed)
 */
static int
dorange(/*@null@*/ const char *arg,
	/*@out@*/ unsigned long *a, /*@out@*/ unsigned long *b,
	unsigned long artno, unsigned long lo, unsigned long hi)
{
    int i;

    if (arg) {
	*a = lo;
	*b = hi;
	i = parserange(arg, a, b);
	if (!(i & RANGE_HAVETO))
	    *b = *a;
    } else {
	*a = *b = artno;
    }

    if ((*b < *a) || (*a > hi) || (*b < lo)) {
	nntpprintf("420 No articles in specified range.");
	return 0;
    }

    /* sanitize */
    if (*b > hi)
	*b = hi;
    if (*a < lo)
	*a = lo;

    return 1;
}
/**
 * This function outputs a list of article headers.
 * You can give a list of patterns, then only headers matching one of
 * these patterns are listed.  If you give NULL as the patterns
 * argument, every header is listed.
 */
static void
doselectedheader(/*@null@*/ const struct newsgroup *group /** current newsgroup */ ,
		 const char *hd /** header to extract */ ,
		 const char *messages /** message range */ ,
		 struct stringlistnode *patterns /** pattern */ ,
		 unsigned long *artno /** currently selected article */)
{
    enum xoverfields OVfield;
    char *l;
    unsigned long a, b = 0;
    long int i, idxa, idxb;
    char *header;

    header = (char *)critmalloc((i = strlen(hd)) + 2, "doselectedheader");
    strcpy(header, hd);
    if (header[i - 1] != ':')
	strcpy(header + i, ":");

    /* HANDLE MESSAGE-ID FORM */
    if (messages && *messages == '<') {
	FILE *f = fopenart(group, messages, artno);
	if (!f) {
	    nntpprintf("430 No such article");
	    free(header);
	    return;
	}
	l = fgetheader(f, header, 1);
	fclose(f);
	if (!l || !(*l)) {
	    nntpprintf_as("221 No such header: %s", hd);
	    fputs(".\r\n", stdout);
	    free(header);
	    return;
	}
	STRIP_TRAILING_SPACE(l);
	if (patterns && !matchlist(patterns, l)) {
	    /* doesn't match any pattern */
	    nntpprintf_as("221 %s matches follow:", hd);
	    fputs(".\r\n", stdout);
	    free(header);
	    free(l);
	    return;
	}
	nntpprintf_as("221 %s %s follow:", hd, (patterns ? "matches" : "headers"));
	printf("%s %s\r\n.\r\n", messages, l ? l : "");
	free(header);
	free(l);
	return;
    }

    /* normal range form chosen */

    if (!group) {
	nntpprintf("412 Use the GROUP command first");
	free(header);
	return;
    }

    markinterest(group->name);

    /* check if header can be obtained from overview */
    OVfield = matchxoverfield(header);

    if (!is_pseudogroup(group->name)) {
	/* FIXME: does this work for local groups? */
	/* is a real group */
	if (xovergroup != group) {
	    if (xgetxover(1, NULL, 0))
		xovergroup = group;
	}
    } else {
	/* is a pseudo group */

	if (patterns) {		/* placeholder matches pseudogroup never */
	    nntpprintf_as("221 %s header matches follow:", hd);
	    fputs(".\r\n", stdout);
	    free(header);
	    return;
	}

	if (OVfield != XO_ERR) {
	    nntpprintf_as("221 First line of %s pseudo-header follows:", hd);
	    printf("%lu ", group->first);
	}
	switch (OVfield) {
	case XO_SUBJECT:
	    printf("Leafnode placeholder for group %s\r\n", group->name);
	    break;
	case XO_FROM:
	    printf("Leafnode <news@%s>\r\n", owndn ? owndn : fqdn);
	    break;
	case XO_DATE:
	    printf("%s\r\n", rfctime());
	    break;
	case XO_MESSAGEID:
	    printf("<leafnode:placeholder:%s@%s>\r\n", group->name,
		   owndn ? owndn : fqdn);
	    break;
	case XO_REFERENCES:
	    printf("\r\n");
	    break;
	case XO_BYTES:
	    printf("%d\r\n", 1024);	/* just a guess */
	    break;
	case XO_LINES:
	    printf("%d\r\n", 22);	/* FIXME: from buildpseudoart() */
	    break;
	case XO_XREF:
	    printf("%s %s:%lu\r\n", fqdn, group->name, group->first);
	    break;
	default:
	    if (!strcasecmp(header, "Newsgroups:")) {
		nntpprintf_as("221 First line of %s pseudo-header follows:", hd);
		printf("%lu %s\r\n", group->first, group->name);
	    } else if (!strcasecmp(header, "Path:")) {
		nntpprintf_as("221 First line of %s pseudo-header follows:", hd);
		printf("%lu %s!not-for-mail\r\n", group->first, owndn ? owndn : fqdn);
	    } else {
		nntpprintf("221 No such header: %s", hd);
	    }
	}
	fputs(".\r\n", stdout);
	free(header);
	return;
    }

    /* HANDLE ARTNO/RANGE FORM */
    if (messages) {
	if (parserange(messages, &a, &b) & RANGE_ERR) {
	    nntpprintf("502 Syntax error");
	    free(header);
	    return;
	}

	if (!dorange(messages, &a, &b, *artno, group->first, group->last)) {
	    free(header);
	    return;
	}
    } else {
	a = b = *artno;
    }

    /* Check whether there are any articles in the range specified */
    if (findxoverrange(a, b, &idxa, &idxb) == -1) {
	nntpprintf("420 No article in specified range.");
	free(header);
	return;
    }
    if (OVfield != XO_ERR) {
	nntpprintf_as("221 %s header %s(from overview) for postings %lu-%lu:",
		   hd, patterns ? "matches " : "", a, b);

	for (i = idxa; i <= idxb; i++) {
	    char *t;
	    l = getxoverfield(xoverinfo[i].text, OVfield);
	    if (!l)
		continue;
	    t = (OVfield == XO_XREF ? l+6 : l); /* cut out 'Xref: ' if necessary */

	    if (patterns && !matchlist(patterns, t)) {
		free(l);
		continue;
	    }

	    printf("%lu %s\r\n", xoverinfo[i].artno, t);
	    free(l);
	}
    } else {
	nntpprintf
	    ("221 %s header %s(from article files) for postings %lu-%lu:",
	     hd, patterns ? "matches " : "", a, b);
	/* as we have the overview anyway, we might as well read the article
	   number from it, even if we have to open the article for the header,
	   saves trying to open non-existing articles */
	for (i = idxa; i <= idxb; i++) {
	    char s[64];
	    unsigned long c = xoverinfo[i].artno;

	    sprintf(s, "%lu", c);
	    l = getheader(s, header);
	    if (!l)
		continue;

	    STRIP_TRAILING_SPACE(l);
	    if (patterns && !matchlist(patterns, l)) {
		free(l);
		continue;
	    }
	    if (*l)
		printf("%lu %s\r\n", c, l);
	    free(l);
	}
    }
    fputs(".\r\n", stdout);
    free(header);
    return;
}static void
doxhdr(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    /* NOTE: XHDR is not to change the current article pointer, thus,
       we're using call by value here */
    struct stringlisthead *l = cmdlinetolist(arg);
    int len = 0;

    if (l)
	len = stringlistlen(l);

    switch (len) {
    case 1:
	doselectedheader(group, l->head->string, NULL, NULL, &artno);
	/* discard changes to artno */
	break;
    case 2:
	doselectedheader(group, l->head->string, l->head->next->string, NULL, &artno);
	/* discard changes to artno */
	break;
    default:
	nntpprintf("502 Usage: XHDR header [{first[-[last]]|<message-id>}]");
    }
    if (l)
	freelist(l);
}

static void
doxpat(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    /* NOTE: XPAT is not to change the current article pointer, thus,
       we're using call by value here */
    struct stringlisthead *l = cmdlinetolist(arg);

    if (!l || stringlistlen(l) < 3) {
	nntpprintf("502 Usage: PAT header first[-[last]] pattern or "
		   "PAT header message-id pattern");
    } else {
	doselectedheader(group, l->head->string, l->head->next->string,
		l->head->next->next, &artno);
	/* discard changes to artno */
    }
    if (l)
	freelist(l);
}


/** implement XOVER command. */
static void
doxover(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    unsigned long a, b;
    long int idx, idxa, idxb;
    int i = 0;

    if (!group) {
	nntpprintf("412 Use the GROUP command first");
	return;
    }

    markinterest(group->name);

    if (!*arg)
	arg = 0;		/* simplify subsequent tests */

    if (arg) {
	i = parserange(arg, &a, &b);
	if (i & RANGE_ERR) {
	    nntpprintf("502 Usage: XOVER [first[-[last]]]");
	    return;
	}
    }

    if (!is_pseudogroup(group->name)) {
	if (xovergroup != group) {
	    freexover();
	    if (xgetxover(1, NULL, 0))
		xovergroup = group;
	    else
		xovergroup = NULL;
	}

	if (!dorange(arg, &a, &b, artno, xfirst, xlast))
	    return;

	if (findxoverrange(a, b, &idxa, &idxb) == -1) {
	    nntpprintf("420 No articles in specified range.");
	} else {
	    nntpprintf
		("224 Overview information for postings %lu-%lu:",
		 a, b);
	    for (idx = idxa; idx <= idxb; idx++) {
		if (xoverinfo[idx].text != NULL) {
		    fputs(xoverinfo[idx].text, stdout);
		    fputs("\r\n", stdout);
		}
	    }
	    fputs(".\r\n", stdout);
	}
    } else {
	/* _is_ pseudogroup */
	if (!(i & RANGE_HAVETO)) b = a;
	if (arg && (b < group->first || a > b || a > group->last+1)) {
	    nntpprintf("420 No articles in specified range.");
	    return;
	}
	nntpprintf_as("224 Overview information (pseudo) for postings %lu-%lu:",
		group->first, group->first);
	nntpprintf_as("%lu\t"
		   "Leafnode placeholder for group %s\t"
		   "news@%s (Leafnode)\t%s\t"
		   "<leafnode:placeholder:%s@%s>\t\t1000\t40\t"
		   "Xref: %s %s:%lu",
		   group->first, group->name,
		   owndn ? owndn : fqdn, rfctime(), group->name,
		   owndn ? owndn : fqdn, fqdn, group->name,
		   group->first);
	fputs(".\r\n", stdout);
    }
}

static int strnum_comp(const void *p1, const void *p2) {
    char *const *s1 = (char * const*) p1;
    char *const *s2 = (char * const*) p2;
    unsigned long u1, u2;
    if (!get_ulong(*s1, &u1)) return 0;
    if (!get_ulong(*s2, &u2)) return 0;
    if (u1 == u2) return 0;
    if (u1 < u2) return -1;
    return 1;
}

static /*@null@*/ /*@dependent@*/ struct newsgroup *
dolistgroup(/*@null@*/ struct newsgroup *group, const char *arg, unsigned long *artno)
{
    struct newsgroup *g;
    int emptygroup = FALSE;
    int pseudogroup;

    if (arg && strlen(arg)) {
	g = findgroup(arg, active, -1);
	if (!g) {
	    nntpprintf("411 No such group: %s", arg);
	    return group;
	} else {
	    opengroup(g);
	    *artno = g->first;
	}
    } else if (group) {
	g = group;
    } else {
	nntpprintf("412 No group specified");
	return 0;
    }
    group = g;
    if ((pseudogroup = is_pseudogroup(g->name))) {
	/* group has not been visited before */
	markinterest(group->name);
    } else if ((xovergroup != group)
	    && chdirgroup(group->name, FALSE)
	    && !xgetxover(1, NULL, 0)) {
	if (is_interesting(g->name)) {
	    /* group has already been marked as interesting but is empty */
	    emptygroup = TRUE;
	}
    } else {
	xovergroup = group;
    }
    markinterest(group->name);
    if (pseudogroup) {
	nntpprintf_as("211 Article list for %s follows (pseudo)", g->name);
	nntpprintf_as("%lu", g->first ? g->first : 1);
    } else if (emptygroup) {
	nntpprintf("211 No articles in %s", g->name);
    } else {
	unsigned long ul;
	char **t = dirlist(".", DIRLIST_ALLNUM, &ul), **i;
	nntpprintf_as("211 Article list for %s follows", g->name);
	if (t) {
	    qsort(t, ul, sizeof(char *), strnum_comp);
	    for (i = t; *i; i++)
		nntpprintf_as("%s", *i);
	    free_dirlist(t);
	}
    }
    nntpprintf(".");
    return group;
}

/*
 * authinfo stuff
 */
/**
 * Read users and their crypt(3)ed passwords into a stringlist.
 * File name: ${sysconfdir}/users
 * File format: line oriented, each line:
 * USER PASSWORD
 * "USER" may not contain a space for obvious reasons.
 * \returns 0 for success, errno otherwise.
 */
static int
readpasswd(void)
{
    char *l;
    FILE *f;
    int error;
    mastr *s = mastr_new(LN_PATH_MAX);

    mastr_vcat(s, sysconfdir, "/users", NULL);
    if ((f = fopen(mastr_str(s), "r")) == NULL) {
	error = errno;
	ln_log(LNLOG_SERR, LNLOG_CTOP, "unable to open %s: %m", mastr_str(s));
	mastr_delete(s);
	return error;
    }
    mastr_delete(s);
    while ((l = getaline(f)) != NULL) {
	initlist(&users);
	appendtolist(users, l);
    }
    if (ferror(f))
	return errno;
    if (fclose(f))
	return errno;
    return 0;
}

static int
isauthorized(void)
{
    if (!authentication)
	return TRUE;
    if (authflag)
	return TRUE;
    nntpprintf("480 Authentication required for command");
    return FALSE;
}

#ifdef USE_PAM
static int
ln_conv(int num_msg, const struct pam_message **msg, struct pam_response  **resp, void *app_data_ptr)
{
    struct pam_response *reply;

    if (num_msg != 1)
	return PAM_CONV_ERR;

    reply = (struct pam_response *) critmalloc(sizeof(struct pam_response), "conv");

    reply->resp_retcode = 0;
    reply->resp = critstrdup((const char *)app_data_ptr, "conv");
	
    *resp = reply;

    return PAM_SUCCESS;
}


static int
doauth_pam(char *const cmd, char *const val)
				/*@modifies *val@*/
{
    static /*@only@*/ char *user = NULL;

    if (0 == strcasecmp(cmd, "user")) {
	if (user)
	    free(user);

	user = critstrdup(val, "doauth_pam");
	memset(val, 0x55, strlen(val));
	return P_CONTINUE;
    }

    if (0 == strcasecmp(cmd, "pass")) {
	pam_handle_t *pamh = NULL;
        int retval;
	struct pam_conv conv;

	conv.conv = ln_conv;
	conv.appdata_ptr = val;

	if (!user) {
	    return P_REJECTED;
	}

        retval = pam_start("leafnode", user, &conv, &pamh);

	if (retval == PAM_SUCCESS)
	    retval = pam_set_item(pamh, PAM_RUSER, user);

	if (retval == PAM_SUCCESS)
	    retval = pam_set_item(pamh, PAM_RHOST, "localhost");

        if (retval == PAM_SUCCESS) {
	    retval = pam_authenticate(pamh, 0);

	    if (retval == PAM_SUCCESS) {
		retval = pam_acct_mgmt(pamh, 0);

		if (pam_end(pamh, retval) != PAM_SUCCESS) {
		    pamh = NULL;
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "cannot release PAM-handle.");
		    exit(1);
		}
		
		if (retval == PAM_SUCCESS)
		    return P_ACCEPTED;
		else {
		    ln_log(LNLOG_SINFO, LNLOG_CTOP, "account-verify failed for %s: %s", user, pam_strerror(pamh, retval));
		    return P_REJECTED;
		}
	    } else {
		ln_log(LNLOG_SINFO, LNLOG_CTOP, "authentication for %s failed: %s", user, pam_strerror(pamh, retval));
		return P_REJECTED;
	    }

	} else
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "pam_start failed: %s", pam_strerror(pamh, retval));	
    }

    memset(val, 0x55, strlen(val));
    return P_SYNTAX_ERROR;
}
#endif

static int
doauth_file(char *const cmd, char *const val)
				/*@modifies *val@*/
{
    static /*@only@*/ char *user = NULL;

    if (0 == strcasecmp(cmd, "user")) {
	char *t;

	if (user)
	    free(user);
	user = (char *)critmalloc(strlen(val) + 2, "doauth_file");
	t = mastrcpy(user, val);
	*t++ = ':';
	*t = '\0';
	memset(val, 0x55, strlen(val));
	return P_CONTINUE;
    }

    if (0 == strcasecmp(cmd, "pass")) {
	char *pwdline;

	if (!user)
	    return P_REJECTED;
	/* XXX hook up other authenticators here
	 * user name + ':' is in "user"
	 * password (cleartext) is in "val" */
	if ((pwdline = findinlist(users, user))) {
	    char *c, *pwd;

	    pwd = pwdline + strlen(user);	/* crypted original password */
	    c = crypt(val, pwd);	/* crypt password from net with
					   original password as salt */
	    memset(val, 0x55, strlen(val));
	    if (strcmp(c, pwd))
		return P_REJECTED;
	    else
		return P_ACCEPTED;
	}
	/* user not found */
	memset(val, 0x55, strlen(val));
	return P_REJECTED;
    }

    memset(val, 0x55, strlen(val));
    return P_SYNTAX_ERROR;
}

static void
doauthinfo(char *arg)
    /*@modifies arg@*/
{				/* we nuke away the password, no const here! */
    char *cmd;
    char *param;
    int result = 0;
    static unsigned int sleeptime = 3;	/* sleep this many seconds after auth failure */

    if (authentication < AM_FILE && authentication > AM_MAX) {
	result = P_NOT_SUPPORTED;
	goto done;
    }

    if (!arg || !*arg) {
	result = P_SYNTAX_ERROR;
	goto done;
    }

    cmd = arg;
    SKIPLWS(cmd);
    param = cmd;
    SKIPWORDNS(param);
    if (!*param || !isspace((unsigned char)*param)) {
	result = P_SYNTAX_ERROR;
	goto done;
    }

    *param++ = '\0';
    SKIPLWS(param);

    switch (authentication) {
#ifdef USE_PAM
    case AM_PAM:
	result = doauth_pam(cmd, param);
	break;
#endif
    case AM_FILE:
	result = doauth_file(cmd, param);
	break;
    default:
	result = P_NOT_SUPPORTED;
	break;
    }

  done:
    switch (result) {
    case P_ACCEPTED:
	nntpprintf("%d Authentication accepted", result);
	authflag = 1;
	break;
    case P_CONTINUE:
	nntpprintf("%d More authentication required", result);
	break;
    case P_REJECTED:
	sleep(sleeptime);
	sleeptime += sleeptime;
	nntpprintf("%d Authentication rejected", result);
	authflag = 0;
	break;
    case P_SYNTAX_ERROR:
	nntpprintf("%d Authentication method not supported", result);
	break;
    case P_NOT_SUPPORTED:
	nntpprintf("%d Authentication not supported", result);
	break;
    default:
	sleep(sleeptime);
	nntpprintf("%d This should not happen: %d", P_SYNTAX_ERROR, result);
	break;
    }				/* switch */
}

static void
log_sockaddr(const char *tag, const struct sockaddr *sa, const char *a)
{
    int h_e;
    char *s = masock_sa2name(sa, &h_e);
    long port = masock_sa2port(sa);

    if (!a) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot convert address to string");
	exit(EXIT_FAILURE);
    }
    if (!s)
	ln_log(LNLOG_SNOTICE, LNLOG_CTOP,
	       "cannot get hostname: %s (h_errno=%d)", my_h_strerror(h_e), h_e);
    ln_log(LNLOG_SINFO, LNLOG_CTOP, "%s: %s:%ld (%s)", tag, s ? s : a, port, a);
    if (s)
	free(s);
}

/* this dummy function is used so we can define a no-op for SIGCHLD */
static void dummy(int unused)
{
    (void)unused;
}

static inline int
mysetfbuf(FILE * f, /*@null@*/ /*@exposed@*/ /*@out@*/ char *buf, size_t size)
{
    return setvbuf(f, buf, _IOFBF, size);
}

static void
main_loop(void)
{
    /* "global" state */
    static unsigned long artno = 0;
    static struct newsgroup *group = 0;

    /* locals */
    char *arg, *cmd;
    int n;
    size_t size;

    while (fflush(stdout), (cmd = mgetaline(stdin))) {
	/* collect possible returned children */
	while (waitpid(-1, 0, WNOHANG) > 0) { }

	if (debugmode & DEBUG_NNTP && !(debugmode & DEBUG_IO))
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "<%s", cmd);

	size = strlen(cmd);
	if (size == 0)
	    continue;		/* necessary for netscape to be quiet */
	else if (size > MAXLINELENGTH || size > INT_MAX) {
	    /* ignore attempts at buffer overflow */
	    nntpprintf("500 Dazed and confused");
	    continue;
	}
	cmd = critstrdup(cmd, "main_loop");
	/* parse command line */
	n = 0;
	while (isspace((unsigned char)cmd[n]))
	    n++;
	while (isalpha((unsigned char)cmd[n]))
	    n++;
	while (isspace((unsigned char)cmd[n]))
	    cmd[n++] = '\0';
	arg = cmd + n;
	while (cmd[n])
	    n++;
	n--;
	while (n>=0 && isspace((unsigned char)cmd[n]))
	    cmd[n--] = '\0';
	if (!strcasecmp(cmd, "quit")) {
	    nntpprintf("205 Always happy to serve!");
	    free(cmd);
	    return;
	}
	if (!strcasecmp(cmd, "article")) {
	    if (isauthorized())
		doarticle(group, arg, 3, &artno);
	} else if (!strcasecmp(cmd, "head")) {
	    if (isauthorized())
		doarticle(group, arg, 2, &artno);
	} else if (!strcasecmp(cmd, "body")) {
	    if (isauthorized())
		doarticle(group, arg, 1, &artno);
	} else if (!strcasecmp(cmd, "stat")) {
	    if (isauthorized())
		doarticle(group, arg, 0, &artno);
	} else if (!strcasecmp(cmd, "help")) {
	    dohelp();
	} else if (!strcasecmp(cmd, "ihave")) {
	    nntpprintf("500 IHAVE is for big news servers");
	} else if (!strcasecmp(cmd, "last")) {
	    if (isauthorized())
		domove(group, -1, &artno);
	} else if (!strcasecmp(cmd, "next")) {
	    if (isauthorized())
		domove(group, 1, &artno);
	} else if (!strcasecmp(cmd, "list")) {
	    if (isauthorized())
		dolist(arg);
	} else if (!strcasecmp(cmd, "mode")) {
	    if (isauthorized())
		domode(arg);
	} else if (!strcasecmp(cmd, "date")) {
	    dodate();
	} else if (!strcasecmp(cmd, "newgroups")) {
	    if (isauthorized())
		donewgroups(arg);
	} else if (!strcasecmp(cmd, "newnews")) {
	    if (isauthorized())
		donewnews(arg);
	} else if (!strcasecmp(cmd, "slave")) {
	    nntpprintf("202 Cool - I always wanted a slave");
	} else if (!strcasecmp(cmd, "post")) {
	    if (allowposting()) {
		if (isauthorized())
		    dopost();
	    } else {
		nntpprintf("440 You are not allowed to post.");
	    }
	} else if (!strcasecmp(cmd, "xhdr") || !strcasecmp(cmd, "hdr")) {
	    if (isauthorized())
		doxhdr(group, arg, artno);
	} else if (!strcasecmp(cmd, "xpat") || !strcasecmp(cmd, "pat")) {
	    if (isauthorized())
		doxpat(group, arg, artno);
	} else if (!strcasecmp(cmd, "xover") || !strcasecmp(cmd, "over")) {
	    if (isauthorized())
		doxover(group, arg, artno);
	} else if (!strcasecmp(cmd, "listgroup")) {
	    if (isauthorized())
		group = dolistgroup(group, arg, &artno);
	} else if (!strcasecmp(cmd, "group")) {
	    if (isauthorized())
		group = dogroup(group, arg, &artno);
	} else if (!strcasecmp(cmd, "authinfo")) {
	    doauthinfo(arg);
	} else {
	    nntpprintf("500 Unknown command");
	}
	free(cmd);
    }
    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	   "Warning: EOF in client input. Timeout or disconnect "
	   "without prior QUIT command.");
    /* We used to send 400 before disconnecting, but the list of clients
     * reported broken keeps growing, and lists tin, slrn and pine,
     * three major text-mode news readers. We anticipate upcoming NNTP
     * standards by silently disconnecting. The 400 error message we
     * used to send was sent in compliance with
     * RFC-977 p. 23 */
}

int
main(int argc, char **argv)
{
    int option;
    socklen_t fodder;
    FILE *se;
    const long bufsize = BLOCKSIZE;
    char *buf = (char *)critmalloc(bufsize, "main");
    char *conffile = NULL;
    const char *const myname = "leafnode";
    int logstderr = 0;

    struct sockaddr_storage sa, peer;

    /* set buffer */
    fflush(stdout);

    mysetfbuf(stdout, buf, bufsize);

    ln_log_open(myname);
    if (!initvars(argv[0], argc > 1 && argv[1] && 0 == strcmp(argv[1], "-e")))
	init_failed(myname);

    while ((option = getopt(argc, argv, GLOBALOPTS "")) != -1) {
	if (!parseopt(myname, option, optarg, &conffile)) {
	    if (option != ':') {
		ln_log(LNLOG_SWARNING, LNLOG_CTOP, "Unknown option %c", option);
	    }
	    exit(EXIT_FAILURE);
	}
	if (option == 'e') {
	    logstderr = 1;
	}
	verbose = 0;		/* overwrite verbose logging */
    }

    if (!logstderr) {
	/* have stderr discarded */
	se = freopen("/dev/null", "w+", stderr);
	if (!se) {
	    ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		    "503 Failure: cannot open /dev/null: %m");
	    exit(EXIT_FAILURE);
	}
    }

    if (readconfig(conffile)) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "503 Server misconfiguration: cannot read configuration file.");
	exit(EXIT_FAILURE);
    }
    if (conffile)
	free(conffile);

    if (filterfile && !readfilter(filterfile)) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "503 Server misconfiguration: cannot read filter file.");
	exit(EXIT_FAILURE);
    }

    if (!init_post())
	init_failed(myname);

    verbose = 0;
    umask((mode_t) 07);

    /* get local address */
    fodder = sizeof(sa);
    if (getsockname(0, (struct sockaddr *)&sa, (socklen_t *) & fodder)) {
	if (errno != ENOTSOCK) {
	    ln_log(LNLOG_SNOTICE, LNLOG_CTOP, "cannot getsockname: %m");
	    printf("503 Cannot getsockname (%s), aborting\r\n",
		   strerror(errno));
	    exit(EXIT_FAILURE);
	}
    } else {
	char *t = masock_sa2addr((struct sockaddr *)&sa);
	log_sockaddr("local  host", (struct sockaddr *)&sa, t);
	if (t) free(t);
    }

    /* get remote address */
    fodder = sizeof(peer);
    if (getpeername(0, (struct sockaddr *)&peer, (socklen_t *) & fodder)) {
	if (errno != ENOTSOCK) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "Connect from unknown client: %m");
	    printf("503 Cannot getpeername (%s), aborting\r\n",
		   strerror(errno));
	    exit(EXIT_FAILURE);
	}
	peeraddr = critstrdup("local file or pipe", myname);
    } else {
	peeraddr = masock_sa2addr((struct sockaddr *)&peer);
	log_sockaddr("remote host", (struct sockaddr *)&peer, peeraddr);
    }

/* #endif */
    if (authentication == AM_FILE && readpasswd() > 0) {
	ln_log_so(LNLOG_SNOTICE, LNLOG_CTOP,
		  "503 Exiting, unable to read user list: %m");
	exit(EXIT_FAILURE);
    }

    /* SIG_IGN would cause ECHILD on wait, so we use our own no-op dummy */
    {
	struct sigaction sact;
	sigemptyset(&sact.sa_mask);
	sact.sa_handler=dummy;
	sact.sa_flags=SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sact, NULL);
    }

    {
	int allow = allowposting();
	nntpprintf("%03d Leafnode NNTP daemon, version %s at %s%s %s",
	       201 - allow, version, owndn ? owndn : fqdn,
	       allow ? "" : " (No posting.)",
	       allow ? "" : NOPOSTING);
    }
    rereadactive();		/* print banner first, so while the
				   client command is in transit, we read
				   the active file. should speed things
				   up by 2 round trips for clients. */
    main_loop();
    freexover();
    freeactive(active);
    active = NULL;
    freelocal();
    freeallfilter(filter);
    freeconfig();
    freeinteresting();
    free_dormant();
    /* Ralf Wildenhues: close stdout before freeing its buffer */
    (void)fclose(stdout);
    free(buf);
    sleep(3); /* defer program exit to avoid recycling process IDs
		 from colliding file names */
    exit(0);
}
