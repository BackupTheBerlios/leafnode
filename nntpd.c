/*
  nntpd -- the NNTP server
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/

#include "leafnode.h"

#include <unistd.h>
#include <assert.h>

#include "critmem.h"
#include "ln_log.h"
#include "get.h"
#include "attributes.h"
#include "mastring.h"
#include "h_error.h"
#include "masock.h"

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

static char *generateMessageID(void);
static /*@dependent@*/ /*@null@*/ FILE *fopenart(/*@null@*/ const struct newsgroup *group, const char *,
		      unsigned long *);
static /*@dependent@*/ /*@null@*/ FILE *buildpseudoart(const char *grp);
static /*@dependent@*/ /*@null@*/ FILE *fopenpseudoart(const struct newsgroup *group, const char *arg,
			    const unsigned long article_num);
static void list(struct newsgroup *ng, int what, char *pattern);
static void main_loop(void);	/* main program loop */
static void doarticle(/*@null@*/ const struct newsgroup *group, const char *arg, int what,
		      unsigned long *);
static /*@null@*/ /*@dependent@*/ struct newsgroup *
dogroup(const char *, unsigned long *);
static void dohelp(void);
static void domode(const char *arg);
static void domove(/*@null@*/ const struct newsgroup *group, int, unsigned long *);
static void dolist(char *p);
static void dodate(void);
static void donewgroups(const char *);
static void donewnews(char *);
static void dopost(void);
static void doxhdr(/*@null@*/ const struct newsgroup *group, const char *,
		   unsigned long artno);
static void doxpat(/*@null@*/ const struct newsgroup *group, const char *,
		   unsigned long artno);
static void doxover(/*@null@*/ const struct newsgroup *group, const char *, unsigned long);
static void doselectedheader(/*@null@*/ const struct newsgroup *, const char *,
			     const char *, struct stringlist *,
			     unsigned long *);
static /*@null@*/ /*@dependent@*/ struct newsgroup *
dolistgroup(/*@null@*/ struct newsgroup *group, const char *,
				     unsigned long *);
static int markinterest(const struct newsgroup *);

static /*@null@*/ /*@only@*/ char *NOPOSTING;
				/* ok-to-print version of getenv("NOPOSTING") */
static int allowposting(void)
    /*@globals undef NOPOSTING@*/;
static int isauthorized(void);
static void doauthinfo(char *arg)
    /*@modifies arg@*/;
static int dorange(/*@null@*/ const char *arg,
		   /*@out@*/ unsigned long *a, /*@out@*/ unsigned long *b,
		   unsigned long artno, unsigned long lo, unsigned long hi);

static /*@dependent@*/ const struct newsgroup *xovergroup;	/* FIXME */
/*@null@*/ static struct stringlist *users = NULL;	/* FIXME */
				/* users allowed to use the server */
int debug = 0;
static int authflag = 0;	/* TRUE if authenticated */

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
    if (debugmode & DEBUG_IO)
	ln_log(LNLOG_SDEBUG, LNLOG_CALL, ">%s", buffer);
    printf("%s\r\n", buffer);
    (void)fflush(stdout);
    va_end(args);
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
	while (waitpid(-1, 0, WNOHANG) > 0);

	if (debugmode & DEBUG_NNTP && !(debugmode & DEBUG_IO))
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "< %s", cmd);

	size = strlen(cmd);
	if (size == 0)
	    continue;		/* necessary for netscape to be quiet */
	else if (size > MAXLINELENGTH || size > INT_MAX) {
	    /* ignore attempts at buffer overflow */
	    nntpprintf("500 Dazed and confused");
	    (void)fflush(stdout);
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
	} else if (!strcasecmp(cmd, "post") && allowposting()) {
	    if (isauthorized())
		dopost();
	} else if (!strcasecmp(cmd, "slave")) {
	    if (isauthorized()) {
		nntpprintf("202 Cool - I always wanted a slave");
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
		group = dogroup(arg, &artno);
	} else if (!strcasecmp(cmd, "authinfo")) {
	    doauthinfo(arg);
	} else {
	    nntpprintf("500 Unknown command");
	}
	free(cmd);
    }
    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
	   "Warning: premature EOF in client input.");
    /* We used to send 400 before disconnecting, but the list of clients
     * reported broken keeps growing, and lists tin, slrn and pine,
     * three major text-mode news readers. We anticipate upcoming NNTP
     * standards by silently disconnecting. The 400 error message we
     * used to send was sent in compliance with
     * RFC-977 p. 23 */
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
	debug = 0;
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
		"\n"
		"Since you have read this dummy article, leafnode will retrieve\n"
		"the newsgroup %s when fetchnews is run\n"
		"the next time. If you'll look into this group a little later, you\n"
		"will see real articles.\n"
		"\n"
		"If you see articles in groups you do not read, that is almost\n"
		"always because of cross-posting.  These articles do not occupy any\n"
		"more space - they are hard-linked into each newsgroup directory.\n"
		"\n"
		"If you do not understand this, please talk to your newsmaster.\n"
		"\n"
		"Leafnode can be found at http://www.leafnode.org/\n\n",
		groupname);
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
    fprintf(f, "From: Leafnode <nobody@%s>\n", owndn ? owndn : fqdn);
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
    if (article_num && article_num == group->first &&
	group->first >= group->last) {
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

/* return FALSE if group is NOT a pseudo group 
 * return TRUE  if group is a pseudo group 
 */
static int
is_pseudogroup(const char *group)
{
    return (!chdirgroup(group, FALSE) && !is_localgroup(group));
}

/* open an article by number or message-id */
/*@null@*/ /*@dependent@*/ static FILE *
fopenart(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long *artno)
/* FIXME: when may artno be touched? */
{
    unsigned long int a;
    FILE *f;
    char *t;
    char s[PATH_MAX + 1];	/* FIXME */
    struct stat st;

    f = NULL;
    t = NULL;
    a = strtoul(arg, &t, 10);
    if (a && t && !*t && group != NULL) {
	if (is_pseudogroup(group->name)) {
	    f = fopenpseudoart(group, arg, a);
	} else {
	    if (is_pseudogroup(group->name) && !is_localgroup(group->name))
		f = fopenpseudoart(group, arg, a);
	    else
		f = fopen(arg, "r");
	}

	if (f)
	    *artno = a;

	markinterest(group);	/* FIXME: check error */
	/* else try message-id lookup */
    } else if (arg && *arg == '<') {
	f = fopen(lookup(arg), "r");
    } else if (group && *artno) {
	(void)chdirgroup(group->name, FALSE);
	sprintf(s, "%lu", *artno);
	f = fopen(s, "r");
	if (!f)
	    f = fopenpseudoart(group, s, *artno);
	/* warning: f may be NULL */
	markinterest(group);	/* FIXME: check error */
    }

    /* do not return articles with zero size (these have been truncated by
     * store.c after a write error) */
    if (f && (fstat(fileno(f), &st) || st.st_size == 0)) {
	(void)fclose(f);
	f = NULL;
    }

    return f;
}

/*
 * Mark an article for download by appending its number to the
 * corresponding file in interesting.groups
 */
static int
markdownload(const struct newsgroup *group, const char *msgid)
{
    char *l;
    FILE *f;
    char s[PATH_MAX + 1];	/* FIXME */

    sprintf(s, "%s/interesting.groups/%s", spooldir, group->name);
    if ((f = fopen(s, "r+"))) {
	while ((l = getaline(f)) != NULL) {
	    if (strncmp(l, msgid, strlen(msgid)) == 0)
		return 0;	/* already marked */
	}
	fprintf(f, "%s\n", msgid);
	ln_log(LNLOG_SDEBUG, LNLOG_CGROUP,
	       "Marking %s: %s for download", group->name, msgid);
	fclose(f);
    }
    return 1;
}

static int
allowposting(void)
    /*@globals undef NOPOSTING@*/
{
    char *s;
    static int initialized = 0;
    static int allowpost = 1;

    /* read NOPOSTING from environment if true */
    if (!initialized) {
	s = getenv("NOPOSTING");
	if (s) {
	    char *p;
	    NOPOSTING = critstrdup(s, "allowposting");
	    for (p = NOPOSTING; *p; p++) {
		if (iscntrl((unsigned char)*p))
		    *p = '_';
	    }
	    allowpost = 0;
	} else {
	    NOPOSTING = NULL;
	}
	initialized = 1;
    }
    return allowpost;
}

/* display an article or somesuch */
/* DOARTICLE */
static void
doarticle(/*@null@*/ const struct newsgroup *group, const char *arg, int what,
	  unsigned long *artno)
{
    FILE *f;
    char *p = NULL;
    unsigned long localartno;
    char *localmsgid = NULL;
    char s[PATH_MAX + 1];	/* FIXME */

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

    sprintf(s, "%3d %lu %s article retrieved - ", 223 - what,
	    localartno, localmsgid);

    if (what == 0)
	strcat(s, "request text separately");
    else if (what == 1)
	strcat(s, "body follows");
    else if (what == 2)
	strcat(s, "head follows");
    else
	strcat(s, "text follows");

    nntpprintf("%s", s);

    /* FIXME: own function */
    while (fgets(s, 1024, f) && *s && (*s != '\n')) {
	if (what & 2) {
	    p = s;
	    if ((p = strchr(p, '\n')))
		*p = '\0';
	    if (*s == '.')
		putc('.', stdout);	/* escape . */
	    fputs(s, stdout);
	    fputs("\r\n", stdout);
	}
    }

    if (what == 3)
	fputs("\r\n", stdout);		/* empty separator line */

    if (what & 1) {
	if (delaybody && *s != '\n') {
	    if (!markdownload(group, localmsgid)) {
		fputs( "\r\n\r\n"
		       "\t[Leafnode:]\r\n"
		       "\t[This message has already been "
		       "marked for download.]\r\n", stdout);
	    } else {
		printf("\r\n\r\n"
		       "\t[Leafnode:]\r\n"
		       "\t[Message %lu of %s]\r\n"
		       "\t[has been marked for download.]\r\n",
		       localartno, group->name);
	    }
	} else {
	    while (fgets(s, 1024, f) && *s) {
		p = s;
		if ((p = strchr(p, '\n')))
		    *p = '\0';
		if (*s == '.')
		  putc('.', stdout);	/* escape . */
		fputs(s, stdout);
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

/* note bug.. need not be _immediately after_ GROUP */
/* returns 0 for success, errno for error */
static int
markinterest(const struct newsgroup *group)
{
    struct stat st;
    struct utimbuf buf;
    int not_yet_interesting;
    time_t now;
    FILE *f;
    mastr *s;
    mastr *s_dormant;

    if (is_localgroup(group->name))
	return 0;		/* local groups don't have to be marked */
    if (is_dormant(group->name))
	return 0;		/* dormant groups don't have to be marked */

    not_yet_interesting = 0;

    s = mastr_new(1024);
    mastr_vcat(s, spooldir, "/interesting.groups/", group->name, 0);

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

    if (not_yet_interesting) {
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

/* change to group - note no checks on group name */
static /*@null@*/ /*@dependent@*/ struct newsgroup *
dogroup(const char *arg, unsigned long *artno)
{
    struct newsgroup *g;
    unsigned long i;
    DIR *d;
    struct dirent *de;
    struct stat st;
    int hardlinks;

    rereadactive();
    g = findgroup(arg, active, -1);
    if (g) {
	freexover();
	xovergroup = NULL;
	if (is_interesting(g->name))
	    markinterest(g);
	if (chdirgroup(g->name, FALSE)) {
	    maybegetxover(g);
	    if (g->count == 0) {
		g->count = (g->last >= g->first ? g->last - g->first + 1 : 0);
		/* FIXME: count articles in group */
		i = 0;
		if (stat(".", &st)) {
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "cannot stat directory of group %s: %m", g->name);
		    hardlinks = 2;
		} else {
		    hardlinks = st.st_nlink;
		}
		if ((d = opendir("."))) {
		    while ((de = readdir(d))) {
			if (de->d_name[0] != '.') {
/* UNIX: skip hidden files */
			    if (!check_allnum(de->d_name)) {
				if (stat(de->d_name, &st)) {
				    ln_log(LNLOG_SWARNING, LNLOG_CGROUP,
					   "warning: cannot stat bogus file "
					   "%s in %s: %m", de->d_name, g->name);
				} else {
				    if (!S_ISDIR(st.st_mode)) {
					if (killbogus) {
					    if (unlink(de->d_name)) {
						ln_log(LNLOG_SERR,
						       LNLOG_CGROUP,
						       "cannot unlink bogus "
						       "file %s in %s: %m",
						       de->d_name, g->name);
					    } else {
						ln_log(LNLOG_SINFO,
						       LNLOG_CGROUP,
						       "unlinked bogus "
						       "file %s in %s",
						       de->d_name, g->name);
					    }
					} else {
					    ln_log(LNLOG_SWARNING,
						   LNLOG_CGROUP,
						   "warning: "
						   "bogus file %s in %s",
						   de->d_name, g->name);
					}

				    } else {
					i++;
				    }
				}
			    } else {
				i++;
			    }
			}
		    }
		    closedir(d);
		    /* UNIX tricks: every subdirectory will have a .., thus,
		       the link count of . is 2 (for . and its name) plus
		       1 for each subdirectory's .. */
		    g->count = i - (hardlinks - 2);
		}
	    }
	    if (g->count == 0) {
		g->first = g->last = 0;	/* FIXME: is this necessary? */
		/* FIXME: is this consistent with other parts (rather
		   set g->first to 1? */
	    }
	} else {		/* group directory is not present */
	    g->first = g->last = g->count = is_pseudogroup(g->name) ? 1ul : 0ul;
	}
	nntpprintf("211 %lu %lu %lu %s group selected",
		   g->count, g->first, g->last, g->name);
	*artno = g->first;
	fflush(stdout);
	return g;
    } else {
	nntpprintf("411 No such group");
	return 0;
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
    char s[PATH_MAX + 1];	/* FIXME */

    by = (by < 0) ? -1 : 1;
    if (group) {
	if (*artno) {
	    *artno += by;
	    do {
		sprintf(s, "%lu", *artno);
		msgid = getheader(s, "Message-ID:");
		if (!msgid)
		    *artno += by;
	    } while (!msgid && *artno >= group->first && *artno <= group->last);
	    if (*artno > group->last) {
		*artno = group->last;
		nntpprintf("421 There is no next article");
	    } else if (*artno < group->first) {
		*artno = group->first;
		nntpprintf("422 There is no previous article");
	    } else {
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
    ng = g;
    while (ng->name) {
	if (what) {
	    if (!pattern)
		printf("%s\t%s\r\n", ng->name, ng->desc ? ng->desc : "-x-");
	    else if (ngmatch(pattern, ng->name) == 0)
		printf("%s\t%s\r\n", ng->name, ng->desc ? ng->desc : "-x-");
	} else {
	    if (!pattern || (ngmatch(pattern, ng->name) == 0))
		printf("%s %010lu %010lu %c\r\n", ng->name, ng->last,
		       ng->first, ng->status);
	}
	ng++;
    }
}

static void
dolist(char *oarg)
{
    char *const arg = critstrdup(oarg, "dolist");

    if (!strcasecmp(arg, "extensions")) {
	nntpprintf("202 extensions supported follow");
	fputs(" HDR\r\n" " OVER\r\n" " PAT\r\n" " LISTGROUP\r\n", stdout);
	if (authentication)
	    printf(" AUTHINFO USER\r\n");
	fputs(".\r\n", stdout);
    } else if (!strcasecmp(arg, "overview.fmt")) {
	nntpprintf("215 information follows");
	fputs("Subject:\r\n"
	       "From:\r\n"
	       "Date:\r\n"
	       "Message-ID:\r\n"
	       "References:\r\n"
	       "Bytes:\r\n" "Lines:\r\n" "Xref:full\r\n" ".\r\n", stdout);
    } else if (!strcasecmp(arg, "active.times")) {
	nntpprintf("215 Placeholder - Leafnode will fetch groups on demand");
	fputs("news.announce.newusers 42 tale@uunet.uu.net\r\n"
	       "news.answers 42 tale@uunet.uu.net\r\n" ".\r\n", stdout);
    } else {
	rereadactive();
	if (!active) {
	    ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		      "503 Group information file does not exist!");
	} else if (!*arg || str_isprefix(arg, "active")) {
	    nntpprintf("215 Newsgroups in form \"group high low flags\".");
	    if (active) {
		if (!*arg || strlen(arg) == 6)
		    list(active, 0, NULL);
		else {
		    char *p = arg;
		    while (*p && (!isspace((unsigned char)*p)))
			p++;
		    while (*p && isspace((unsigned char)*p))
			p++;
		    list(active, 0, p);
		}
	    }
	    fputs(".\r\n", stdout);
	} else if (str_isprefix(arg, "newsgroups")) {
	    nntpprintf("215 Descriptions in form \"group description\".");
	    if (active) {
		if (strlen(arg) == 10)
		    list(active, 1, NULL);
		else {
		    char *p = arg;
		    while (*p && (!isspace((unsigned char)*p)))
			p++;
		    while (*p && isspace((unsigned char)*p))
			p++;
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
parsedate_newnews(const struct stringlist *l, const int gmt)
{
    struct tm timearray;
    time_t age;
    long a, b;

    if (stringlistlen(l) < 2) return (time_t)-1;
    memset(&timearray, 0, sizeof(timearray));
    a = strtol(l->string, NULL, 10);
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
    a = strtol(l->next->string, NULL, 10);
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
donew_common(const struct stringlist *l)
{
    int gmt, len;
    time_t age;

    len = stringlistlen(l);
    if (len < 2) {
	nntpprintf("502 Syntax error");
	return -1;
    }

    gmt = (len >= 3 && !strcasecmp(l->next->next->string, "gmt"));

    age = parsedate_newnews(l, gmt);
    if (age == (time_t)-1) {
	nntpprintf("502 Syntax error");
	return -1;
    }

    return age;
}

static void
donewnews(char *arg)
{
    struct stringlist *l = cmdlinetolist(arg);
    struct stat st;
    time_t age;
    DIR *d, *ng;
    struct dirent *de, *nga;
    char s[PATH_MAX + 1];	/* FIXME */

    if (!l) {
	nntpprintf("502 Syntax error.");
	return;
    }
    age = donew_common(l->next);
    if (age == (time_t)-1) {
	freelist(l);
	return;
    }

    nntpprintf("230 List of new articles since %ld in newsgroup %s", age,
	       l->string);
    sprintf(s, "%s/interesting.groups", spooldir);
    d = opendir(s);
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Unable to open directory %s: %m", s);
	fputs(".\r\n", stdout);
	freelist(l);
	return;
    }
    while ((de = readdir(d))) {
	if (ngmatch((const char *)&(l->string), de->d_name) == 0) {
	    chdirgroup(de->d_name, FALSE);
	    getxover(1);
	    ng = opendir(".");
	    while ((nga = readdir(ng))) {
		unsigned long artno;

		if (get_ulong(nga->d_name, &artno)) {
		    if ((stat(nga->d_name, &st) == 0) &&
			(*nga->d_name != '.') && S_ISREG(st.st_mode) &&
			(st.st_ctime > age)) {
			long xo = findxover(artno);

			if (xo >= 0) {
			    char *x = cuttab(xoverinfo[xo].text, XO_MESSAGEID);
			    if (x) {
				fputs(x, stdout);
				fputs("\r\n", stdout);
				free(x);
			    } else {
				/* FIXME: cannot find message ID in XOVER */
			    }
			} else {
			    /* FIXME: cannot find XOVER for article */
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
    fputs(".\r\n", stdout);
}

static void
donewgroups(const char *arg)
{
    struct stringlist *l = cmdlinetolist(arg);
    time_t age;
    struct newsgroup *ng;

    age = donew_common(l);
    if (age == (time_t)-1) {
	freelist(l);
	return;
    }

    nntpprintf("231 List of new newsgroups since %lu follows",
	       (unsigned long)age);
    ng = active;
    while (ng->name) {
	if (ng->age >= age)
	    printf("%s %lu %lu %c\r\n", ng->name, ng->first, ng->last, ng->status);
	ng++;
    }
    fputs(".\r\n", stdout);
    freelist(l);
}

/* next bit is copied from INN 1.4 and modified("broken") by agulbra
   mail to Rich $alz <rsalz@uunet.uu.net> bounced */
/* Scale time back a bit, for shorter Message-ID's. */
#define OFFSET	673416000L
static char ALPHABET[] = "0123456789abcdefghijklmnopqrstuv";
static char *
generateMessageID(void)
{
    static char buff[80];
    static time_t then;
    static unsigned int fudge;
    time_t now;
    char *p;
    int n;

    now = time(0);		/* might be 0, in which case fudge 
				   will almost fix it */
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
    *p++ = '.';
    n = fudge * 32768 + (int)getpid();
    while (n) {
	*p++ = ALPHABET[(int)(n & 31)];
	n >>= 5;
    }
    sprintf(p, ".ln@%s>", owndn ? owndn : fqdn);
    return (critstrdup(buff, "generateMessageID"));
}

/* the end of what I stole from rsalz and then mangled */

/* checks for malformatted newsgroups header */
/* returns 0 if malformatted (spurious whitespace) */
static int
validate_newsgroups(const char *n)
{
    char *t;

    if (str_isprefix(n, "Newsgroups:")) {
	n += 11;
	/* skip separating whitespace */
	n += strspn(n, WHITESPACE);
    }

    /* find other whitespace */
    t = strpbrk(n, WHITESPACE);
    if (t) {
#if 0
	/* tolerant version */
	/* we have found whitespace, check if it's just trailing */
	if (strspn(t, WHITESPACE) != strlen(t, WHITESPACE))
	    return 0;
#else
	/* pedantic version */
	return 0;
#endif
    }
    return 1;
}

/* check for correct Message-ID, feed with n pointing to first MID char */
/* this version is pedantic: no whitespace, no weird characters */
static int
validate_messageid(const char *n)
{
    if (*n != '<')
	return 0;
    while (*++n != '\0' && *n != '>') {
	if (isspace(*n) || iscntrl(*n))
	    return 0;
    }
    if (*n++ != '>')
	return 0;
    SKIPLWS(n);
    if (*n != '\0')
	return 0;
    return 1;
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
    char inname[PATH_MAX + 1];
    static int postingno;	/* starts at 0 */

    do {
	sprintf(inname, "%s/in.coming/%d-%lu-%d",
		spooldir, (int)getpid(), (unsigned long)time(NULL),
		++postingno);
	out = open(inname, O_WRONLY | O_EXCL | O_CREAT, (mode_t) 0444);
    } while (out < 0 && errno == EEXIST);

    if (out < 0) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "441 Unable to open spool file %s: %m", inname);
	return;
    }

    msgid = generateMessageID();
    nntpprintf("340 Ok, recommended ID %s", msgid);
    fflush(stdout);
    /* get headers */
    do {
	line = getaline(stdin);

	if (!line) {
	    /* client died */
	    log_unlink(inname);
	    nntpprintf("400 Premature end of input. Exiting.");
	    exit(0);
	}

	/* premature end (no body) */
	if (!strcmp(line, ".")) {
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
	    else
		havenewsgroups = TRUE;
	}

	if (str_isprefix(line, "Date:")) {
	    if (havedate)
		err = TRUE;
	    else
		havedate = TRUE;
	}

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
	if (len && line[len - 1] == '\n')
	    line[--len] = '\0';
	if (len && line[len - 1] == '\r')
	    line[--len] = '\0';
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
	}
	writes(out, "\r\n");
    } while (*line);

    free(msgid);
    /* get bodies */
    if (strcmp(line, ".")) {	/* skip if header contained a single dot line */
	havebody = TRUE;
	do {
	    debug = 0;
	    line = getaline(stdin);
	    debug = debugmode;
	    if (!line) {
		log_unlink(inname);
		exit(EXIT_FAILURE);
	    }
	    len = strlen(line);
	    if (len && line[len - 1] == '\n')
		line[--len] = '\0';
	    if (len && line[len - 1] == '\r')
		line[--len] = '\0';
	    if (line[0] == '.') {
		/* escape or skip if single dot */
		if (len > 1) {
		    write(out, line + 1, len - 1);
		    writes(out, "\r\n");
		}
	    } else {
		write(out, line, len);
		writes(out, "\r\n");
	    }
	} while (strcmp(line, "."));
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

	f = fopen(inname, "r");
	if (f) {
	    mid = fgetheader(f, "Message-ID:", 1);
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
	    log_unlink(inname);
	    return;
	}

	if (!validate_newsgroups(groups))
	{
	    nntpprintf
		("441 Spurious whitespace in Newsgroups: header, fix your news reader");
	    log_unlink(inname);
	    goto cleanup;
	}

	if (havemessageid) {
	    struct stat st;

	    if (!validate_messageid(mid)) {
		nntpprintf("441 Invalid Message-ID: header, article not posted");
		log_unlink(inname);
		goto cleanup;
	    }

	    if (stat(lookup(mid), &st) == 0) {
		nntpprintf("441 435 Duplicate, article not posted");
		log_unlink(inname);
		goto cleanup;
	    }
	}

	if ((forbidden = checkstatus(groups, 'n'))) {
	    /* Newsgroups: contains a group to which posting is not allowed */
	    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		   "Article was posted to non-writable group %s", forbidden);
	    nntpprintf("441 Posting to %s not allowed", forbidden);
	    log_unlink(inname);
	    goto cleanup;
	}

	if ((modgroup = checkstatus(groups, 'm'))) {
	    /* Post to a moderated group */
	    moderator = getmoderator(modgroup);
	    if (!moderator && is_alllocal(modgroup)) {
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "Did not find moderator address for local moderated "
		       "group %s", modgroup);
		nntpprintf("503 Configuration error: No moderator for %s",
			   modgroup);
		log_unlink(inname);
		goto cleanup;
	    }
	    approved = getheader(inname, "Approved:");
	}

	if (!moderator && !is_alllocal(groups)) {
	    /* also posted to external groups or moderated group with
	       unknown moderator, store into out.going */
	    char s[PATH_MAX + 1];	/* FIXME: overflow possible */
	    outbasename = strrchr(inname, '/');
	    outbasename++;
	    sprintf(s, "%s/out.going/%s", spooldir, outbasename);
	    if (sync_link(inname, s)) {
		ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		       "unable to schedule for posting to upstream, "
		       "link %s -> %s failed: %m", inname, s);
		nntpprintf("503 Could not schedule article for posting "
			   "to upstream, see syslog.");
		log_unlink(inname);
		goto cleanup;
	    }
	    if (modgroup && !approved) {
		nntpprintf("240 Posting scheduled for posting to "
			   "upstream, be patient");
		log_unlink(inname);
		goto cleanup;
	    }
	}

	if (moderator && !approved) {
	    /* Mail the article to the moderator */
	    int fd;

	    fd = open(inname, O_RDONLY);
	    if (fd < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "Couldn't open article %s: %m",
		       inname);
	    } else if (mailto(moderator, fd)) {
		nntpprintf("503 Posting to moderator %s failed: %m", moderator);
	    } else {
		ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		       "Message %s mailed to moderator %s", inname, moderator);
		nntpprintf("240 Article mailed to moderator");
	    }

	    if (fd >= 0)
		log_close(fd);
	    log_unlink(inname);
	    goto cleanup;
	}

	ln_log(LNLOG_SINFO, LNLOG_CTOP, "%s POST %s %s",
	       is_alllocal(groups) ? "LOCAL" : "UPSTREAM", mid, groups);

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
	    if (lockfile_exists(FALSE)) {
		ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		       "Cannot obtain lock file to store article %s. "
		       "It will be posted later.", inname);
		_exit(EXIT_FAILURE);
	    }

	    rereadactive();
	    feedincoming();
	    writeactive();
	    log_unlink(lockfile);
	    _exit(0);

	default:
	    nntpprintf("240 Article posted, now be patient");
	    break;
	}			/* switch(fork()) */

cleanup:
	if (mid) free(mid);
	if (groups) free(groups);
	if (modgroup) free(modgroup);
	if (moderator) free(moderator);
	if (forbidden) free(forbidden);
	if (approved) free(approved);
	return;
    }

    log_unlink(inname);
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

void
doxhdr(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    /* NOTE: XHDR is not to change the current article pointer, thus,
       we're using call by value here */
    struct stringlist *l = cmdlinetolist(arg);

    switch (stringlistlen(l)) {
    case 1:
	doselectedheader(group, l->string, NULL, NULL, &artno);
	/* discard changes to artno */
	break;
    case 2:
	doselectedheader(group, l->string, l->next->string, NULL, &artno);
	/* discard changes to artno */
	break;
    default:
	nntpprintf("502 Usage: XHDR header [{first[-[last]]|<message-id>}]");
    }
    if (l)
	freelist(l);
}

void
doxpat(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    /* NOTE: XPAT is not to change the current article pointer, thus,
       we're using call by value here */
    struct stringlist *l = cmdlinetolist(arg);

    if (stringlistlen(l) < 3) {
	nntpprintf("502 Usage: PAT header first[-[last]] pattern or "
		   "PAT header message-id pattern");
    } else {
	doselectedheader(group, l->string, l->next->string, l->next->next,
			 &artno);
	/* discard changes to artno */
    }
    if (l)
	freelist(l);
}

/**
 * This function outputs a list of article headers.
 * You can give a list of patterns, then only headers matching one of
 * these patterns are listed.  If you give NULL as the patterns
 * argument, every header is listed.
 */
void
doselectedheader(/*@null@*/ const struct newsgroup *group /** current newsgroup */ ,
		 const char *hd /** header to extract */ ,
		 const char *messages /** message range */ ,
		 struct stringlist *patterns /** pattern */ ,
		 unsigned long *artno /** currently selected article */)
{
    /* FIXME: this is bloody complex and hard to follow */
    const char *const h[] = { "Subject:", "From:", "Date:", "Message-ID:",
	"References:", "Bytes:", "Lines:"
    };
    int OVfield;
    char *l;
    unsigned long a, b = 0, c;
    long int i;
    FILE *f;
    struct stringlist *ap;
    char *header;

    header = (char *)critmalloc((i = strlen(hd)) + 2, "doselectedheader");
    strcpy(header, hd);
    if (header[i - 1] != ':')
	strcpy(header + i, ":");

    /* HANDLE MESSAGE-ID FORM */
    if (messages && *messages == '<') {
	f = fopenart(group, messages, artno);
	if (!f) {
	    nntpprintf("430 No such article");
	    free(header);
	    return;
	}
	l = fgetheader(f, header, 1);
	if (!l || !(*l)) {
	    nntpprintf("221 No such header: %s", hd);
	    fputs(".\r\n", stdout);
	    fclose(f);
	    free(header);
	    return;
	}
	STRIP_TRAILING_SPACE(l);
	if (patterns) {
	    ap = patterns;
	    while (ap) {
		if (0 == ngmatch((const char *)&(ap->string), l))
		    break;
		ap = ap->next;
	    }
	    if (!ap) {		/* doesn't match any pattern */
		nntpprintf("221 %s matches follow:", hd);
		fputs(".\r\n", stdout);
		fclose(f);
		free(header);
		free(l);
		return;
	    }
	}
	nntpprintf("221 %s %s follow:", hd, (patterns ? "matches" : "headers"));
	printf("%s %s\r\n.\r\n", messages, l ? l : "");
	fclose(f);
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

    markinterest(group);

    /* check if header can be obtained from overview */
    OVfield = sizeof(h) / sizeof(h[0]);
    do {
	OVfield--;
    } while (OVfield >= 0 && strcasecmp(h[OVfield], header));

    if (!is_pseudogroup(group->name)) {
	/* FIXME: does this work for local groups? */
	/* is a real group */
	if (xovergroup != group) {
	    if (getxover(1))
		xovergroup = group;
	}
    } else {
	/* is a pseudo group */

	if (patterns) {		/* placeholder matches pseudogroup never */
	    nntpprintf("221 %s header matches follow:", hd);
	    fputs(".\r\n", stdout);
	    free(header);
	    return;
	}

	if (OVfield < 0) {
	    if (!strcasecmp(header, "Newsgroups:")) {
		nntpprintf("221 First line of %s pseudo-header follows:", hd);
		printf("1 %s\r\n", group->name);
		fputs(".\r\n", stdout);
	    } else if (!strcasecmp(header, "Path:")) {
		nntpprintf("221 First line of %s pseudo-header follows:", hd);
		printf("1 %s!not-for-mail\r\n", owndn ? owndn : fqdn);
		fputs(".\r\n", stdout);
	    } else {
		nntpprintf("221 No such header: %s", hd);
		fputs(".\r\n", stdout);
	    }
	    free(header);
	    return;
	}

	nntpprintf("221 First line of %s pseudo-header follows:", hd);
	if (OVfield == 0)	/* Subject: */
	    printf("1 Leafnode placeholder for group %s\r\n", group->name);
	else if (OVfield == 1)	/* From: */
	    printf("1 Leafnode <nobody@%s>\r\n", owndn ? owndn : fqdn);
	else if (OVfield == 2)	/* Date: */
	    printf("1 %s\r\n", rfctime());
	else if (OVfield == 3)	/* Message-ID: */
	    printf("1 <leafnode:placeholder:%s@%s>\r\n", group->name,
		   owndn ? owndn : fqdn);
	else if (OVfield == 4)	/* References */
	    printf("1 (none)\r\n");	/* FIXME */
	else if (OVfield == 5)	/* Bytes */
	    printf("1 %d\r\n", 1024);	/* just a guess */
	else if (OVfield == 6)	/* Lines */
	    printf("1 %d\r\n", 22);	/* FIXME: from buildpseudoart() */
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
    i = -1;
    c = a;
    while ((c <= b) && (i == -1)) {
	i = findxover(c);
	c++;
    }
    if (i == -1) {
	nntpprintf("420 No article in specified range.");
	free(header);
	return;
    }
    if (OVfield >= 0) {
	nntpprintf("221 %s header %s(from overview) for postings %lu-%lu:",
		   hd, patterns ? "matches " : "", a, b);

	for (c = a; c <= b; c++) {
	    char *t = 0;
	    if (xoverinfo &&
		c >= xfirst && c <= xlast && (i = findxover(c)) >= 0) {
		char *li = xoverinfo[i].text;
		int d;

		/* OK MA 2001-06-27 */
		for (d = 0; li && d <= OVfield; d++) {
		    li = strchr(li, '\t');
		    if (li)
			li++;
		}

		if (li) {
		    char *p;
		    p = strchr(li, '\t');
		    if (!p)
			p = li + strlen(li);

		    t = (char *)critmalloc(p - li + 1, "doselectedheader");
		    mastrncpy(t, li, p - li + 1);
		}

		if (patterns) {
		    ap = patterns;
		    if (!t) continue;
		    while (ap) {
			if (!ngmatch((const char *)&(ap->string), t))
			    break;
			ap = ap->next;
		    }
		    if (!ap) {
			if (t) free(t);
			continue;
		    }
		}

		if (t) {
		    printf("%lu %s\r\n", c, t);
		    free(t);
		}
	    }
	}
    } else {
	nntpprintf
	    ("221 %s header %s (from article files) for postings %lu-%lu:",
	     hd, patterns ? "matches " : "", a, b);
	for (c = a; c <= b; c++) {
	    char s[64];

	    sprintf(s, "%lu", c);
	    l = getheader(s, header);
	    if (l) {
		STRIP_TRAILING_SPACE(l);
		if (patterns) {
		    ap = patterns;
		    while (ap) {
			if (!ngmatch((const char *)&(ap->string), l))
			    break;
			ap = ap->next;
		    }
		    if (!ap) {
			free(l);
			continue;
		    }
		}
		if (*l)
		    printf("%lu %s\r\n", c, l);
		free(l);
	    }
	}
    }
    fputs(".\r\n", stdout);
    free(header);
    return;
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

/** implement XOVER command. */
void
doxover(/*@null@*/ const struct newsgroup *group, const char *arg, unsigned long artno)
{
    unsigned long a, b, art;
    long int idx;
    int flag = FALSE;
    int i;

    if (!group) {
	nntpprintf("412 Use the GROUP command first");
	return;
    }

    markinterest(group);

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
	    if (getxover(1))
		xovergroup = group;
	    else
		xovergroup = NULL;
	}

	if (!dorange(arg, &a, &b, artno, xfirst, xlast))
	    return;

	for (art = a; art <= b; art++) {
	    idx = findxover(art);
	    if (idx >= 0 && xoverinfo[idx].text != NULL) {
		if (!flag) {
		    flag = TRUE;
		    nntpprintf
			("224 Overview information for postings %lu-%lu:",
			 a, b);
		}
		printf("%s\r\n", xoverinfo[idx].text);
	    }
	}
	if (flag)
	    fputs(".\r\n", stdout);
	else
	    nntpprintf("420 No articles in specified range.");
    } else {
	/* _is_ pseudogroup */
	if ((b < 1) || (a > 1) || (a > b)) {
	    nntpprintf("420 No articles in specified range.");
	    return;
	}
	nntpprintf("224 Overview information (pseudo) for postings 1-1:");
	nntpprintf("%lu\t"
		   "Leafnode placeholder for group %s\t"
		   "nobody@%s (Leafnode)\t%s\t"
		   "<leafnode.%s@%s>\t\t1000\t40",
		   b, group->name,
		   owndn ? owndn : fqdn, rfctime(), group->name,
		   owndn ? owndn : fqdn);
	fputs(".\r\n", stdout);
    }
}

/*@null@*/ /*@dependent@*/ struct newsgroup *
dolistgroup(/*@null@*/ struct newsgroup *group, const char *arg, unsigned long *artno)
{
    struct newsgroup *g;
    int emptygroup = FALSE;
    long int idx;
    unsigned long art;
    int pseudogroup;

    if (arg && strlen(arg)) {
	g = findgroup(arg, active, -1);
	if (!g) {
	    nntpprintf("411 No such group: %s", arg);
	    return 0;
	} else {
	    group = g;
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
	markinterest(group);
    } else if ((xovergroup != group) && !getxover(1)) {
	if (is_interesting(g->name)) {
	    /* group has already been marked as interesting but is empty */
	    emptygroup = TRUE;
	}
    } else {
	xovergroup = group;
    }
    markinterest(group);
    if (pseudogroup) {
	nntpprintf("211 Article list for %s follows (pseudo)", g->name);
	printf("%lu \r\n", g->last ? g->last : 1);
    } else if (emptygroup) {
	nntpprintf("211 No articles in %s", g->name);
    } else {
	nntpprintf("211 Article list for %s (%lu-%lu) follows",
		   g->name, xfirst, xlast);
	for (art = xfirst; art <= xlast; art++) {
	    idx = findxover(art);
	    if (idx >= 0 && xoverinfo[idx].text)
		printf("%lu \r\n", art);
	}
    }
    fputs(".\r\n", stdout);
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
    struct stringlist *ptr = NULL;
    char s[PATH_MAX + 1];	/* FIXME */

    sprintf(s, "%s/users", libdir);
    if ((f = fopen(s, "r")) == NULL) {
	error = errno;
	ln_log(LNLOG_SERR, LNLOG_CTOP, "unable to open %s: %m", s);
	return error;
    }
    while ((l = getaline(f)) != NULL) {
	appendtolist(&users, &ptr, l);
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

static int
doauth_file(char *const cmd, char *const val)
				/*@modifies *val@*/
{
    static /*@only@*/ char *user = NULL;

    if (0 == strcasecmp(cmd, "user")) {
	char *t;

	if (user)
	    free(user);
	user = critmalloc(strlen(val) + 2, "doauth_file");
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
	 * user name + blank is in "user"
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

void
doauthinfo(char *arg)
    /*@modifies arg@*/
{				/* we nuke away the password, no const here! */
    char *cmd;
    char *param;
    int result = 0;

    if (!authentication) {
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
	nntpprintf("%d This should not happen: %d", P_SYNTAX_ERROR, result);
	break;
    }				/* switch */
}

static void
log_sockaddr(const char *tag, const struct sockaddr *sa)
{
    int h_e;
    char *s = masock_sa2name(sa, &h_e);
    char *a = masock_sa2addr(sa);
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
    free(a);			/* a == NULL caught above */
}

/* this dummy function is used so we can define a no-op for SIGCHLD */
static RETSIGTYPE dummy(int unused);
static RETSIGTYPE
dummy(int unused)
{
    (void)unused;
}

static int
mysetfbuf(FILE * f, /*@null@*/ /*@exposed@*/ /*@out@*/ char *buf, size_t size)
{
#ifdef SETVBUF_REVERSED
    return setvbuf(f, _IOFBF, buf, size);
#else
    return setvbuf(f, buf, _IOFBF, size);
#endif
}

int
main(int argc, char **argv)
{
    int option, reply, err;
    socklen_t fodder;
    char conffile[PATH_MAX + 1];
    FILE *se;
    const long bufsize = BLOCKSIZE;
    char *buf = (char *)critmalloc(bufsize, "main");

#ifdef HAVE_IPV6
    struct sockaddr_in6 sa, peer;
#else
    struct sockaddr_in sa, peer;
#endif

    whoami();

    /* set buffer */
    fflush(stdout);

    mysetfbuf(stdout, buf, bufsize);

    if (((err = snprintf(conffile, sizeof(conffile), "%s/config", libdir)) < 0)
	|| (err >= (int)sizeof(conffile))) {
	exit(EXIT_FAILURE);
    }

    ln_log_open("leafnode");
    if (!initvars(argv[0]))
	exit(EXIT_FAILURE);


    /* have stderr discarded */
    se = freopen("/dev/null", "w+", stderr);
    if (!se) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "503 Failure: cannot open /dev/null: %m");
	exit(EXIT_FAILURE);
    }

    while ((option = getopt(argc, argv, "F:D:V")) != -1) {
	if (!parseopt("leafnode", option, optarg, conffile, sizeof(conffile))) {
	    ln_log(LNLOG_SWARNING, LNLOG_CTOP, "Unknown option %c", option);
	    exit(EXIT_FAILURE);
	}
	debug = debugmode;
	verbose = 0;		/* overwrite verbose logging */
    }

    if ((reply = readconfig(conffile)) != 0) {
	ln_log_so(LNLOG_SERR, LNLOG_CTOP,
		  "503 Unable to read configuration from %s: %s",
		  conffile, strerror(reply));
	exit(EXIT_FAILURE);
    }

    verbose = 0;
    umask((mode_t) 02);

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
	log_sockaddr("local  host", (struct sockaddr *)&sa);
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
    } else {
	log_sockaddr("remote host", (struct sockaddr *)&peer);
    }

/* #endif */
    if (authentication && (reply = readpasswd()) > 0) {
	ln_log_so(LNLOG_SNOTICE, LNLOG_CTOP,
		  "503 Exiting, unable to read user list: %m");
	exit(EXIT_FAILURE);
    }
    signal(SIGCHLD, dummy);	/* SIG_IGN would cause ECHILD on wait, so we use
				   our own no-op dummy */
    {
	int allow = allowposting();
	printf("%03d Leafnode NNTP daemon, version %s at %s%s %s\r\n",
	       201 - allow, version, owndn ? owndn : fqdn,
	       allow ? "" : " (No posting.)",
	       allow ? "" : NOPOSTING);
    }
    (void)fflush(stdout);
    rereadactive();		/* print banner first, so while the
				   client command is in transit, we read
				   the active file. should speed things
				   up by 2 round trips for clients. */
    main_loop();
    freexover();
    freeactive(active);
    active = NULL;
    freelocal();
    freeconfig();
    freeinteresting();
    /* Ralf Wildenhues: close stdout before freeing its buffer */
    (void)fclose(stdout);
    free(buf);
    exit(0);
}
