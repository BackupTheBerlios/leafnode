/*
nntpd -- the NNTP server

Written by Arnt Gulbrandsen <agulbra@troll.no> and copyright 1995
Troll Tech AS, Postboks 6133 Etterstad, 0602 Oslo, Norway, fax +47
22646949.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
and Randolf Skerka <Randolf.Skerka@gmx.de>. Copyright of the modifications
1997.
Modified by Kent Robotti <robotti@erols.com>. Copyright of the
modifications 1998.
Modified by Markus Enzenberger <enz@cip.physik.uni-muenchen.de>.
Copyright of the modifications 1998.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
and Kazushi (Jam) Marukawa <jam@pobox.com>. Copyright of the modifications
1998, 1999.
Modified by Matthias Andree <ma@dt.e-technik.uni-dortmund.de>
Copyright of the modifications 1999.
Modified By Joerg Dietrich <joerg@dietrich.net>
Copyright of the modifications 2000.

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "get.h"

#ifdef SOCKS
#include <socks.h>
#endif

#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#define __USE_XOPEN   /* for glibc's crypt() */
#include <unistd.h>
#include <utime.h>

#define MAXLINELENGTH 1000

/* for authorization: */
#define P_NOT_SUPPORTED	503
#define P_SYNTAX_ERROR	501
#define P_REJECTED	452
#define P_CONTINUE	350
#define P_ACCEPTED	250

int write2(int fd, const char *msg);
int hash (const char *);
FILE * fopenart(const char *);
FILE * buildpseudoart(const char * grp);
FILE * fopenpseudoart(const char * arg, const unsigned long article_num);
void list(struct newsgroup * ng, int what, char * pattern);
void rereadactive(void);

void parser(void);
void error(const char *msg);
void doarticle(const char *arg, int what);
void dogroup(const char *);
void dohelp(void);
void domode(const char *arg);
void domove(int);
void dolist(char *p);
void donewgroups(const char *);
void donewnews(char*);
void dopost(void);
void doxhdr(char *);
void doxpat(char *);
void doselectedheader(const char *, const char *, struct stringlist *);
void doxover(const char *);
void dolistgroup(const char *);
void markinterest(void);
int isauthorized(void);
void doauthinfo(const char *arg);

struct newsgroup * group;	/* current group, initially none */
struct newsgroup * xovergroup = NULL;
struct stringlist * users = NULL;
				/* users allowed to use the server */
int pseudogroup;		/* indicates whether we are in a "real"
				   group or pretend only */
unsigned long int artno;	/* current article number */
static char *cmd;		/* current command line */
time_t activetime;

int debug = 0;
int authflag = 0;		/* TRUE if authenticated */
time_t gmt_off;			/* offset between localtime and GMT in sec */

static jmp_buf timeout;

static void timer(int sig) {
    longjmp(timeout, 1);
    exit(sig);			/* not reached */
}

/*
 * calculate offset in seconds between GMT and localtime.
 * Code by Joerg Dietrich <joerg@dietrich.net>.
 */
static time_t gmtoff(void) {
    time_t localsec, gmtsec;
    time_t now;
    struct tm * ltime;
    char *zone, *oldzone;
    size_t len;

    now = time(NULL);		/* returns UTC */
    ltime = localtime(&now);
    localsec = mktime(ltime);

    zone = getenv("TZ");	/* save current timezone */
    len = 4 + (zone ? strlen(zone) : 0);
    oldzone = (char *)calloc(len, sizeof(char));
    if (!oldzone) {
	ln_log(LNLOG_NOTICE, "Unable to allocate %d chars, using GMT", len);
	return(0);
    }
    snprintf(oldzone, len, "TZ=%s", zone ? zone : "");
    putenv("TZ=GMT");		/* use mktime to create UTC */
    gmtsec = mktime(ltime);
    putenv(oldzone);
    free(oldzone);

    return(gmtsec - localsec);
}

/*
 * call getaline with 15 min timeout (not configurable)
 */
static char * mgetaline(FILE *f) {
    char * l;

    if (setjmp(timeout)) {
	return NULL;
    }
    (void) signal(SIGALRM, timer);
    (void) alarm(15*60);
    l = getaline(f);
    (void) alarm(0U);
    return l;
}

/*
 * this function avoids the continuous calls to both syslog and printf
 * it also appends \r\n automagically
 * slightly modified from a function nntp_log_and_reply() written by
 * Matthias Andree <ma@dt.e-technik.uni-dortmund.de> (c) 1999
 */
static void nntpprintf(const char *fmt, ...) {
    char buffer[1024];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (debugmode > 1)
	ln_log(LNLOG_DEBUG, ">%s", buffer);
    printf("%s\r\n", buffer);
    fflush(stdout);
    va_end(args);
}

void rereadactive(void) {
    struct stat st;

    strcpy(s, spooldir);
    strcat(s, "/leaf.node/groupinfo");

    if ((!stat(s, &st) && (st.st_mtime > activetime)) ||
	 (active==NULL)) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG, "rereading %s", s);
	readactive();
	activetime = st.st_mtime;
    }
}



void error(const char *msg) {
    printf("%s: %s\r\n", msg, strerror(errno));
}


void parser(void) {
    char *arg;
    int n;

    while ((cmd=mgetaline(stdin))) {
	n = strlen(cmd);
	if (n == 0)
	    continue;		/* necessary for netscape to be quiet */
	else if (n > MAXLINELENGTH) {
	    /* ignore attempts at buffer overflow */
	    nntpprintf("500 Dazed and confused");
	    continue;
	}

	/* parse command line */
	n = 0;
	while (isspace((unsigned char)cmd[n]))
	    n++;
	while (isalpha((unsigned char)cmd[n]))
	    n++;
	while (isspace((unsigned char)cmd[n]))
	    cmd[n++] = '\0';
	
	arg = cmd+n;

	while(cmd[n])
	    n++;
	n--;
	while(isspace((unsigned char)cmd[n]))
	    cmd[n--] = '\0';

	if (!strcasecmp(cmd, "quit")) {
	    nntpprintf("205 Always happy to serve!");
	    return;
	}
	if (!strcasecmp(cmd, "article")) {
	    if (isauthorized())
		doarticle(arg, 3);
	} else if (!strcasecmp(cmd, "head")) {
	    if (isauthorized())
		doarticle(arg, 2);
	} else if (!strcasecmp(cmd, "body")) {
	    if (isauthorized())
		doarticle(arg, 1);
	} else if (!strcasecmp(cmd, "stat")) {
	    if (isauthorized())
		doarticle(arg, 0);
	} else if (!strcasecmp(cmd, "help")) {
	    dohelp();
	} else if (!strcasecmp(cmd, "ihave")) {
	    nntpprintf("500 IHAVE is for big news servers");
	} else if (!strcasecmp(cmd, "last")) {
	    if (isauthorized())
		domove(-1);
	} else if (!strcasecmp(cmd, "next")) {
	    if (isauthorized())
		domove(1);
	} else if (!strcasecmp(cmd, "list")) {
	    if (isauthorized())
		dolist(arg);
	} else if (!strcasecmp(cmd, "mode")) {
	    if (isauthorized())
		domode(arg);
	} else if (!strcasecmp(cmd, "newgroups")) {
	    if (isauthorized())
		donewgroups(arg);
	} else if (!strcasecmp(cmd, "newnews")) {
	    if (isauthorized())
		donewnews(arg);
	} else if (!strcasecmp(cmd, "post")) {
	    if (isauthorized())
		dopost();
	} else if (!strcasecmp(cmd, "slave")) {
	    if (isauthorized()) {
		nntpprintf("202 Cool - I always wanted a slave");
	    }
	} else if (!strcasecmp(cmd, "xhdr")) {
	    if (isauthorized())
		doxhdr(arg);
	} else if (!strcasecmp(cmd, "xpat")) {
	    if (isauthorized())
	    	doxpat(arg);
	} else if (!strcasecmp(cmd, "pat")) {
	    if (isauthorized())
		doxpat(arg);
	} else if (!strcasecmp(cmd, "xover")) {
	    if (isauthorized())
		doxover(arg);
	} else if (!strcasecmp(cmd, "over")) {
	    if (isauthorized())
		doxover(arg);
	} else if (!strcasecmp(cmd, "listgroup")) {
	    if (isauthorized())
		dolistgroup(arg);
	} else if (!strcasecmp(cmd, "group")) {
	    if (isauthorized())
		dogroup(arg);
	} else if (!strcasecmp(cmd, "authinfo")) {
	    doauthinfo(arg);
	} else {
	    nntpprintf("500 Unknown command");
	}
	fflush(stdout);
    }
    nntpprintf("421 Network error: %s", strerror(errno));
}

/*
 * pseudo article stuff
 */

static void fprintpseudobody(FILE * pseudoart, const char * groupname) {
    FILE * f;
    char * l, * cl, *c;

    if (pseudofile && ((f = fopen(pseudofile, "r")) != NULL)) {
	while ((l = getaline(f)) != NULL) {
	    cl = l;
	    while ((c = strchr(cl, '%')) != NULL) {
		if (strncmp(c, "%%", 2) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%%", cl);
		    cl = c + 2;
		}
		else if (strncmp(c, "%server", 7) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, fqdn);
		    cl = c + 7;
		}
		else if (strncmp(c, "%version", 8) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, version);
		    cl = c + 8;
		}
		else if (strncmp(c, "%newsgroup", 10) == 0) {
		    *c = '\0';
		    fprintf(pseudoart, "%s%s", cl, groupname);
		    cl = c + 10;
		}
		else {
		    *c = '\0';
		    fprintf(pseudoart, "%s%%", cl);
		    cl = c + 1;
		}
	    }
	    fprintf(pseudoart, "%s\n", cl);
	}
	fclose(f);
    } else {
	if (pseudofile)
	    ln_log(LNLOG_NOTICE, "Unable to read pseudoarticle from %s: %s",
		    pseudofile, strerror(errno));
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
	  "always because of cross-posting.  These articles do not occupy any\n"          "more space - they are hard-linked into each newsgroup directory.\n"
	  "\n"
	  "If you do not understand this, please talk to your newsmaster.\n"
	  "\n"
	  "Leafnode can be found at http://www.leafnode.org/\n\n", groupname);
    }
}

/* build and return an open fd to pseudoart in group */
FILE * buildpseudoart(const char * grp) {
    FILE * f;

    f = tmpfile();
    if (!f) {
	ln_log(LNLOG_ERR, "Could not create pseudoarticle for group %s", grp);
	return f;
    }
    ln_log(LNLOG_INFO, "Creating pseudoarticle for group %s", grp);

    fprintf(f, "Path: %s\n", owndn ? owndn : fqdn);
    fprintf(f, "Newsgroups: %s\n", grp);
    fprintf(f, "From: Leafnode <nobody@%s>\n", fqdn);
    fprintf(f, "Subject: Leafnode placeholder for group %s\n", grp);
    fprintf(f, "Date: %s\n", rfctime());
    fprintf(f, "Message-ID: <leafnode:placeholder:%s@%s>\n", grp, fqdn);
    fprintf(f, "\n");
    fprintpseudobody(f, grp);

    rewind(f);
    return f;
}

/* open a pseudo art */
FILE * fopenpseudoart(const char * arg, const unsigned long article_num) {
    FILE *f = NULL;
    char msgidbuf[128];
    char * c;
    struct newsgroup * g;    

    if (debugmode)
	ln_log(LNLOG_DEBUG, "%s: first %lu last %lu artno %lu",
    		group->name, group->first, group->last, article_num);
    if (article_num && article_num == group->first &&
	 group->first >= group->last) {
	f = buildpseudoart(group->name);
    } else if (!article_num) {
	if (!strncmp(arg, "<leafnode:placeholder:", 22)) {
	    strncpy(msgidbuf, arg+22, 128);
	    msgidbuf[127] = '\0';
	    if ((c = strchr(msgidbuf, '@')) != NULL) {
		*c = '\0';
		g = findgroup(msgidbuf);
		if (g)
		    f = buildpseudoart(g->name);
	    }
	}
    }
    return f;
}


/* open an article by number or message-id */
FILE * fopenart(const char * arg) {
    unsigned long int a;
    FILE *f;
    char *t;

    f = NULL;
    t = NULL;
    a = strtoul(arg, &t, 10);
    if (a && t && !*t && group) {
	if (pseudogroup && (!islocalgroup(group->name)))
	    f = fopenpseudoart(arg, a);
	else {
	    f = fopen(arg, "r");
	    if (!f && (!islocalgroup(group->name)))
		f = fopenpseudoart(arg, a);
	}
	if (f)
	    artno = a;
	markinterest();
	/* else try message-id lookup */
    } else if (arg && *arg=='<') {
	f = fopen(lookup(arg), "r");
    } else if (group && artno) {
	sprintf(s, "%lu", artno);
	f = fopen(s, "r");
	if (!f)
	    f = fopenpseudoart(s, a);
	markinterest();
    }
    return f;
}


/*
 * Mark an article for download by appending its number to the
 * corresponding file in interesting.groups
 */
static int markdownload(char * msgid) {
    char *l;
    FILE *f;

    sprintf(s, "%s/interesting.groups/%s", spooldir, group->name);
    if ((f = fopen(s, "r+"))) {
	while ((l = getaline(f)) != NULL) {
	    if (strncmp(l, msgid, strlen(msgid)) == 0)
	        return 0; /* already marked */
	}
	fprintf(f, "%s\n", msgid);
	ln_log(LNLOG_DEBUG, "Marking %s: %s for download", group->name, msgid);
	fclose(f);
    }
    return 1;
}

/* display an article or somesuch */
/* DOARTICLE */
void doarticle(const char *arg, int what) {
    FILE *f;
    char *p = NULL;
    char *q = NULL;
    unsigned long localartno;
    char *localmsgid = NULL;

    f = fopenart(arg);
    if (!f) {
	if (arg && *arg != '<' && !group)
	    nntpprintf("412 No newsgroup selected");
	else if (strlen(arg))
	    nntpprintf("430 No such article: %s", arg);
	else
	    nntpprintf("423 No such article: %lu", artno);
	return;
    }

    if (!arg) {
	localartno = artno;
	localmsgid = fgetheader(f, "Message-ID:");
    }
    else if (*arg == '<') {
	localartno = 0;
	localmsgid = strdup(arg);
    }
    else {
	localartno = strtoul(arg, NULL, 10);
	localmsgid = fgetheader(f, "Message-ID:");
    }

    if (!localartno) {
	/* we have to extract the article number from the Xref: header */
	p = fgetheader(f, "Xref:");
	if (p) {
	    /* search article number of 1st group in Xref: */
	    q = strchr(p, ':');
	    if (q) {
	        q++;
		localartno = strtoul(q, NULL, 10);
	    }
	    if (group) {
		/* search article number of group in Xref: */
		q = strstr(p, group->name);
		if (q) {
		    q += strlen(group->name);
		    if (*q++ == ':')
		        localartno = strtoul(q, NULL, 10);
		}
	    }
	    free(p);
	}
    }

    sprintf(s, "%3d %lu %s article retrieved - ", 223-what,
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

    while (fgets(s, 1024, f) && *s && (*s!='\n')) {
	if (what & 2) {
	    p = s;
	    if ((p = strchr(p, '\n')))
		*p = '\0';

	    if(*s=='.') putc('.', stdout); /* escape . */
	    fputs(s, stdout);
	    fputs("\r\n", stdout);
	}
    }

    if (what==3)
	printf("\r\n"); /* empty separator line */

    if (what & 1) {
	if (delaybody && *s != '\n') {
	    if (! markdownload(localmsgid)) {
		printf("\r\n\r\n"
			"\t[Leafnode:]\r\n"
			"\t[This message has already been "
			"marked for download.]\r\n");
	    } else {
		printf("\r\n\r\n"
			"\t[Leafnode:]\r\n"
			"\t[Message %ld of %s]\r\n"
			"\t[has been marked for download.]\r\n",
			localartno, group->name);
	    }
	} else {
	    while (fgets(s, 1024, f) && *s) {
		p = s;
		if ((p = strchr(p, '\n')))
		    *p = '\0';
		printf("%s%s\r\n", *s=='.' ? "." : "", s);
	    }
	}
    }
    if (what)
	printf(".\r\n");
    fclose(f);
    free(localmsgid);

    return; /* FIXME: OF COURSE there were no errors */
}


/* note bug.. need not be _immediately after_ GROUP */
void markinterest(void) {
    struct stat st;
    struct utimbuf buf;
    int error;
    time_t now;
    FILE * f;

    if (islocalgroup(group->name))
	return;		/* local groups don't have to be marked */
    error = 0;
    sprintf(s, "%s/interesting.groups/%s", spooldir, group->name);
    if (stat(s, &st) == 0) {
	now = time(0);
	buf.actime = (now < st.st_atime) ? st.st_atime : now;
			/* now < update may happen through HW failures */
	buf.modtime = st.st_mtime;
	if (utime(s, &buf))
	    error = 1;
    }
    else
	error = 1;
    if (error) {
	f = fopen(s, "w");
	if (f)
	    fclose(f);
	else
	    ln_log(LNLOG_ERR, "Could not create %s: %s", s, strerror(errno));
    }
}
    


/* change to group - note no checks on group name */
void dogroup(const char *arg) {
    struct newsgroup * g;
    int i;
    DIR * d;
    struct dirent * de;

    rereadactive();
    g = findgroup(arg);
    if (g) {
	group = g;
	if (isinteresting(group->name))
	    markinterest();
	if (chdirgroup(g->name, FALSE)) {
	    if (g->count == 0) {
		g->count = (g->last >= g->first ? g->last-g->first+1 : 0) ;
		/* count articles in group */
		i = 0;
		if ((d = opendir(".")) != NULL) {
		    while ((de = readdir(d)) != NULL)
		        i++;
		    g->count = i - 2;	/* for "." and ".." */
		}
	    }
	    nntpprintf("211 %lu %lu %lu %s",
		   g->count, g->first, g->last, g->name);
	    pseudogroup = FALSE;
	} else {
	    nntpprintf("211 1 1 1 %s",
		    g->name);
	    g->first = 1;
	    g->last  = 1;
	    g->count = 1;
	    artno = 1;
	    pseudogroup = TRUE;
	}
	artno = g->first;
	fflush(stdout);
    } else {
	nntpprintf("411 No such group");
    }
}

void dohelp(void) {
    printf("100 Legal commands\r\n");
/*  printf("  authinfo user Name|pass Password|generic <prog> <args>\r\n"); */
    printf("  authinfo user Name|pass Password\r\n");
    printf("  article [MessageID|Number]\r\n");
    printf("  body [MessageID|Number]\r\n");
/*  printf("  date\r\n"); */
    printf("  group newsgroup\r\n");
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
    printf("  newnews newsgroups yymmdd hhmmss [\"GMT\"] [<distributions>]\r\n");
    printf("  next\r\n");
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

void domode(const char *arg) {
    if (!strcasecmp(arg, "reader"))
	nntpprintf("200 Leafnode %s, pleased to meet you!", version);
    else
	nntpprintf("500 MODE other than READER not supported");
}

void domove(int by) {
    char * msgid;

    by = (by < 0) ? -1 : 1 ;
    if (group) {
	if (artno) {
	    artno += by ;
	    do {
		sprintf(s, "%lu", artno);
		msgid = getheader(s, "Message-ID:");
		if (!msgid)
		    artno += by;
	    } while (!msgid && artno >= group->first && artno <= group->last);
	    if (artno > group->last) {
		artno = group->last;
		nntpprintf("421 There is no next article");
	    }
	    else if (artno < group->first) {
		artno = group->first;
		nntpprintf("422 There is no previous article");
	    }
	    else {
	        nntpprintf("223 %lu %s article retrieved", artno, msgid);
	    }
	    free(msgid);
	} else {
	    nntpprintf("420 There is no current article");
	}
    } else {
	nntpprintf("412 No newsgroup selected");
    }
}



/* LIST ACTIVE if what==0, else LIST NEWSGROUPS */
void list(struct newsgroup *g, int what, char * pattern) {
    struct newsgroup * ng;
    char * p;

    /* if pattern exists, convert to lowercase */
    if (pattern && *pattern) {
	p = pattern;
	while (p && *p) {
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
	    if (!pattern)
		printf("%s %010lu %010lu y\r\n", ng->name, ng->last, ng->first);
	    else if (ngmatch(pattern, ng->name) == 0)
		printf("%s %010lu %010lu y\r\n", ng->name, ng->last, ng->first);
	}
	ng++;
    }
}
	
void dolist(char *arg) {
    char * p = NULL;

    if (!strcasecmp(arg, "extensions")) {
	nntpprintf("202 extensions supported follow");
	printf(" OVER\r\n"
	       " PAT\r\n"
	       " LISTGROUP\r\n");
	if (authentication) printf(" AUTHINFO USER\r\n");
	printf(	".\r\n");
    } else if (!strcasecmp(arg, "overview.fmt")) {
	nntpprintf("215 information follows");
	printf("Subject:\r\n"
	       "From:\r\n"
	       "Date:\r\n"
	       "Message-ID:\r\n"
	       "References:\r\n"
	       "Bytes:\r\n"
	       "Lines:\r\n"
	       "Xref:full\r\n"
	       ".\r\n");
    } else if (!strcasecmp(arg, "active.times")) {
	nntpprintf("215 Placeholder - Leafnode will fetch groups on demand");
	printf("news.announce.newusers 42 tale@uunet.uu.net\r\n"
	       "news.answers 42 tale@uunet.uu.net\r\n"
	       ".\r\n");
    } else {
	rereadactive();
	if (!active) {
	    ln_log_so(LNLOG_ERR, "503 Group information file does not exist!");
	} else if (!*arg || !strncasecmp(arg, "active", 6)) {
	    nntpprintf("215 Newsgroups in form \"group high low flags\".");
	    if (active) {
		if (!*arg || strlen(arg) == 6)
		    list(active, 0, NULL);
		else {
		    while (*arg && (!isspace((unsigned char)*arg)))
			arg++;
		    while (*arg && isspace((unsigned char)*arg))
			arg++;
		    p = arg;
		    list (active, 0, arg);
		}
	    }
	    printf(".\r\n");
	} else if (!strncasecmp(arg, "newsgroups", 10)) {
	    nntpprintf("215 Descriptions in form \"group description\".");
	    if (active) {
		if (strlen(arg) == 10)
		    list(active, 1, NULL);
		else {
		    while (*arg && (!isspace((unsigned char)*arg)))
			arg++;
		    while (*arg && isspace((unsigned char)*arg))
			arg++;
		    list (active, 1, arg);
		}
	    }
	    printf(".\r\n");
	 } else {
	    nntpprintf("503 Syntax error");
	 }
    }
}

void donewnews(char *arg) {
    struct stringlist *l = cmdlinetolist(arg);
    struct stat st;
    struct tm timearray;
    struct tm * ltime;
    time_t age;
    time_t now;
    int year, century;
    int a, b;
    DIR * d, * ng;
    struct dirent * de, *nga;

    if (stringlistlen(l) < 3) {
	nntpprintf("502 Syntax error");
	return;
    }

    now = time(0);
    ltime = localtime(&now);
    year    = ltime->tm_year % 100;
    century = ltime->tm_year / 100;  /* 0 for 1900-1999, 1 for 2000-2099 etc */

    memset(&timearray, 0, sizeof(timearray));
    a = strtol(l->next->string, NULL, 10);
    /* NEWNEWS may have the form YYMMDD or YYYYMMDD.
       Distinguish between the two */
    b = a / 10000;
    if (b < 100) {
	/* YYMMDD */
	if (b <= year)
	    timearray.tm_year = b + (century*100) ;
	else
	    timearray.tm_year = b + (century-1)*100 ;
    } else if (b < 1000) {
    	/* YYYMMDD - happens with buggy newsreaders */
	/* In these readers, YYY=100 is equivalent to YY=00 or YYYY=2000 */
	ln_log(LNLOG_NOTICE,
		"NEWGROUPS year is %d: please update your newsreader", b);
	timearray.tm_year = b ;
    } else {
	/* YYYYMMDD */
	timearray.tm_year = b - 1900 ;
    }
    timearray.tm_mon  = (a % 10000 / 100) - 1 ;
    timearray.tm_mday = a % 100 ;
    a = strtol(l->next->next->string, NULL, 10);
    timearray.tm_hour = a / 10000;
    timearray.tm_min  = a % 10000 / 100;
    timearray.tm_sec  = a % 100;
    /* mktime() shall guess correct value of tm_isdst (0 or 1) */
    timearray.tm_isdst = -1;
    /* mktime() assumes local time -> correct according to timezone
       (global variable set by last call to localtime()) if GMT is
       not requested */
    age = mktime(&timearray) - (strstr(arg, "GMT") ? gmt_off : 0);

    nntpprintf("230 List of new articles in newsgroup %s", l->string);

    sprintf(s, "%s/interesting.groups", spooldir);
    d = opendir(s);
    if (!d) {
	ln_log(LNLOG_ERR, "Unable to open directory %s: %s", s,
	       strerror(errno));
	printf(".\r\n");
	return;
    }
    while ((de = readdir(d)) != NULL) {
	if (ngmatch((const char*)&(l->string), de->d_name) == 0) {
	    chdirgroup(de->d_name, FALSE);
	    getxover();
	    ng = opendir(".");
	    while ((nga = readdir(ng)) != NULL) {
		long artno;
		if(get_long(nga->d_name, &artno)) {
		    if ((stat(nga->d_name, &st) == 0) &&
			(*nga->d_name != '.') && S_ISREG(st.st_mode) &&
			(st.st_ctime > age)) {
			
			long xo=findxover(artno);
			if(xo >= 0) {
			    char *x = cuttab(xoverinfo[xo].text, XO_MESSAGEID);
			    if(x) {
				fputs(x, stdout);
				fputs("\r\n", stdout);
				free(x);
			    } else {
				/* FIXME: cannot find message ID in XOVER */
			    }
			} else {
			    /* FIXME: cannot find XOVER for article */
			}
		    } /* too old */
		} /* if not a number, not an article */
	    } /* readdir loop */
	    closedir(ng);
	    free(xoverinfo);
	    xoverinfo = 0;
	}
    }
    closedir(d);
    fputs(".\r\n", stdout);
}

void donewgroups(const char *arg) {
    struct tm timearray;
    struct tm * ltime;
    time_t age;
    time_t now;
    int year, century;
    char * l;
    int a, b;
    struct newsgroup * ng;

    now = time(0);
    ltime = localtime(&now);
    year    = ltime->tm_year % 100;
    century = ltime->tm_year / 100;  /* 0 for 1900-1999, 1 for 2000-2099 etc */

    memset(&timearray, 0, sizeof(timearray));
    l = NULL;
    a = strtol(arg, &l, 10);
    /* NEWGROUPS may have the form YYMMDD or YYYYMMDD.
       Distinguish between the two */
    b = a / 10000;
    if (b < 100) {
	/* YYMMDD */
	if (b <= year)
	    timearray.tm_year = b + (century*100) ;
	else
	    timearray.tm_year = b + (century-1)*100 ;
    } else if (b < 1000) {
    	/* YYYMMDD - happens with buggy newsreaders */
	/* In these readers, YYY=100 is equivalent to YY=00 or YYYY=2000 */
	ln_log(LNLOG_NOTICE,
		"NEWGROUPS year is %d: please update your newsreader", b);
	timearray.tm_year = b ;
    } else {
	/* YYYYMMDD */
	timearray.tm_year = b - 1900 ;
    }
    timearray.tm_mon  = (a % 10000 / 100) - 1 ;
    timearray.tm_mday = a % 100 ;
    while (*l && isspace((unsigned char)*l))
	l++;
    a = strtol(l, NULL, 10);	/* we don't care about the rest of the line */
    timearray.tm_hour = a / 10000;
    timearray.tm_min  = a % 10000 / 100;
    timearray.tm_sec  = a % 100;
    /* mktime() shall guess correct value of tm_isdst (0 or 1) */
    timearray.tm_isdst = -1;
    /* mktime() assumes local time -> correct according to timezone
       (global variable set by last call to localtime()) if GMT is not
       requested */
    age = mktime(&timearray) - (strstr(arg, "GMT") ? gmt_off : 0);

    nntpprintf("231 List of new newsgroups since %d follows", age);

    ng = active;
    while (ng->name) {
	if (ng->age >= age)
	    printf("%s %lu %lu y\r\n", ng->name, ng->first, ng->last);
	ng++;
    }
    printf(".\r\n");
}

/* next bit is copied from INN 1.4 and modified ("broken") by agulbra

   mail to Rich $alz <rsalz@uunet.uu.net> bounced */

/* Scale time back a bit, for shorter Message-ID's. */
#define OFFSET	673416000L

static char ALPHABET[] = "0123456789abcdefghijklmnopqrstuv";

char * generateMessageID(void);

char * generateMessageID(void) {
    static char buff[80];
    static time_t then;
    static unsigned int fudge;
    time_t now;
    char * p;
    int n;

    now = time(0); /* might be 0, in which case fudge will almost fix it */
    if (now != then)
	fudge = 0;
    else
	fudge++;
    then = now;

    p = buff;
    *p++ = '<';
    n = (unsigned int)now - OFFSET;
    while(n) {
	*p++ = ALPHABET[(int)(n & 31)];
	n >>= 5;
    }
    *p++ = '.';
    n = fudge * 32768 + (int)getpid();
    while(n) {
	*p++ = ALPHABET[(int)(n & 31)];
	n >>= 5;
    }
    sprintf(p, ".ln@%s>", fqdn);
    return buff;
}

/* the end of what I stole from rsalz and then mangled */


void dopost(void) {
    char * line;
    int havefrom = 0;
    int havepath = 0;
    int havedate = 0;
    int havenewsgroups = 0;
    int havemessageid = 0;
    int havesubject = 0;
    int err = 0;
    int hdrtoolong = FALSE;
    size_t i;
    size_t len;
    int out;
    char outname[80];
    static int postingno; /* starts as 0 */

    
    sprintf(outname, "%s/out.going/%d-%d-%d",
	     spooldir, (int)getpid(), (int)time(NULL), ++postingno);

    out = open(outname, O_WRONLY|O_EXCL|O_CREAT, 0444);
    if (out < 0) {
	char *error = strerror(errno);
	ln_log_so(LNLOG_ERR, "441 Unable to open spool file %s: %s",
		outname, error);
	return;
    }

    nntpprintf("340 Go ahead.");
    fflush(stdout);

    /* get headers */
    do {
	size_t len;
	debug = 0;
	line = getaline(stdin);
	debug = debugmode;
	if (!line) {
	    unlink(outname);
	    exit(0);
	}
	if (!strncasecmp(line, "From:", 5)) {
	    if (havefrom)
		err = TRUE;
	    else
		havefrom = TRUE;
	}
	if (!strncasecmp(line, "Path:", 5)) {
	    if (havepath)
		err = TRUE;
	    else
		havepath = TRUE;
	}
	if (!strncasecmp(line, "Message-ID:", 11)) {
	    if (havemessageid)
		err = TRUE;
	    else
		havemessageid = TRUE;
	}
	if (!strncasecmp(line, "Subject:", 8)) {
	    if (havesubject)
		err = TRUE;
	    else
		havesubject = TRUE;
	}
	if (!strncasecmp(line, "Newsgroups:", 11)) {
	    if (havenewsgroups)
		err = TRUE;
	    else
		havenewsgroups = TRUE;
	}
	if (!strncasecmp(line, "Date:", 5)) {
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

	/* replace tabs with spaces */
	for (i = 0; i < len; i++)
	    if (isspace((unsigned char)line[i]))
		line[i] = ' ';
	if (len && line[len-1]=='\n')
	    line[--len] = '\0';
	if (len && line[len-1]=='\r')
	    line[--len] = '\0';
	if (len && line[len-1]=='\r')
	    line[--len] = '\0';
	if (len)
	    write(out, line, len);
	else {
	    if (!havepath) {
		write(out, "Path: ", 6);
		if (owndn)
		    write(out, owndn, strlen(owndn));
		else
		    write(out, fqdn, strlen(fqdn));
		write(out, "!nobody\r\n", 9);
	    }
	    if (!havedate) {
		const char * l = rfctime();
		write(out, "Date: ", 6);
		write(out, l, strlen(l));
		write(out, "\r\n", 2);
	    }
	    if (!havemessageid) {
		char tmp[80];
		sprintf(tmp, "Message-ID: %s\r\n",
			 generateMessageID());
		write(out, tmp, strlen(tmp));
	    }
	}
	write(out, "\r\n", 2);
    } while (*line);

    /* get bodies */
    do {
	debug = 0;
	line = getaline(stdin);
	debug = debugmode;
	if (!line) {
	    unlink(outname);
	    exit(EXIT_FAILURE);
	}

	len = strlen(line);
	if (len && line[len-1]=='\n')
	    line[--len] = '\0';
	if (len && line[len-1]=='\r')
	    line[--len] = '\0';
	if (len && line[len-1]=='\r')
	    line[--len] = '\0';
	if (line[0] == '.') {
	    if (len > 1) {
		write(out, line+1, len-1);
		write(out, "\r\n", 2);
	    }
	}
	else {
	    write(out, line, len);
	    write(out, "\r\n", 2);
	}
    } while (line[0] != '.' || line[1] != '\0');
    close(out);

    if (havefrom && havesubject && havenewsgroups && !err) {
	FILE * f;
	char * mid;
	char * groups;
	char * outbasename;
	
	f = fopen(outname, "r");
	mid = fgetheader(f, "Message-ID:");
	groups = fgetheader(f, "Newsgroups:");
	fclose(f);
	switch (fork()) {
	case -1: {
	    ln_log(LNLOG_ERR, "fork: %s",strerror(errno));
	    nntpprintf("503 Could not store article in newsgroups");
	    unlink(outname);
	    break;
	}
	case 0: {
	    if (lockfile_exists(TRUE, TRUE)) {
		/* Something is really wrong. Move the article 
		   to failed.postings */
		ln_log(LNLOG_ERR, "Could not store article %s." 
			"Moving it to failed.postings.", outname);
		outbasename = strrchr(outname, '/');
		outbasename++;
		sprintf(s, "%s/failed.postings/%s", spooldir, outbasename);
		if (link(outname, s))
                    ln_log(LNLOG_ERR, 
			   "unable to move failed posting to %s: %s",
			   s, strerror(errno));
		unlink(outname);
		_exit(EXIT_FAILURE);
	    }
	    rereadactive();
	    storearticle(outname, mid, groups);
	    writeactive();
	    /* if article is only posted in local groups, don't feed it
	       to fetchnews */
	    unlink(lockfile);
	    gfixxover(groups);
	    if (islocal(groups)) {
		unlink(outname);	/* No error check. If this fails,
					 * delposted() will do the job
					 */
	    }
	    _exit(0);
	}
	default: {
	    nntpprintf("240 Article posted, now be patient");
	    break;
	}
	
	} /* switch (fork()) */ 
	
	return;
    }

    unlink(outname);
    if (!havefrom)
	nntpprintf("441 From: header missing, article not posted");
    else if (!havesubject)
	nntpprintf("441 Subject: header missing, article not posted");
    else if (!havenewsgroups)
	nntpprintf("441 Newsgroups: header missing, article not posted");
    else if (hdrtoolong)
	nntpprintf("441 Header too long, article not posted");
    else
	nntpprintf("441 Formatting error, article not posted");
}



void doxhdr(char *arg) {
    struct stringlist *l = cmdlinetolist(arg);
    switch (stringlistlen(l)) {
	case 1: {
	    doselectedheader(l->string, NULL, NULL);
	    break;
	}
	case 2: {
	    doselectedheader(l->string, l->next->string, NULL);
	    break;
	}
	default: {
	    nntpprintf("502 Usage: XHDR header first[-last] or "
			"XHDR header message-id");
	}
    }
    if (l)
	freelist(l);
}

void doxpat(char *arg) {
    struct stringlist *l = cmdlinetolist(arg);

    if (stringlistlen(l) < 3) {
	nntpprintf("502 Usage: XPAT header first[-last] pattern or "
		    "XPAT header message-id pattern");
    }
    else {
	doselectedheader(l->string, l->next->string, l->next->next);
    }

    if (l)
	freelist(l);
}

#define STRIP_TRAILING_SPACE(str) \
        do { \
            if ((str) && *(str)) { \
                int __len = strlen(str); \
                char *__tmp = (str) + __len - 1; \
                if (__len > 1) { \
                    while (__tmp && *__tmp \
                            && isspace((unsigned char)*__tmp)) \
                        *__tmp-- = '\0'; \
                } \
            } \
        } while (0)

/*
 * This function outputs a list of article headers.
 * You can give a list of patterns, then only headers matching one of
 * these patterns are listed.  If you give NULL as the patterns
 * argument, every header is listed.
 */
void doselectedheader(const char *header, const char *messages,
                      struct stringlist *patterns)
{
    char * h[] = { "Subject", "From", "Date", "Message-ID", "References",
		   "Bytes", "Lines", "Newsgroups"
    }; /* ugly emacs workaround */

    int n;
    int dash = 0;
    char *l;
    unsigned long a, b = 0, c;
    long int i;
    FILE *f;

    struct stringlist *ap;
    int mp = (patterns != NULL); /* Do pattern matching ? */
    if (messages && *messages == '<') { /* handle message-id form (well) */
        f = fopenart(messages);
	if (!f) {
	    nntpprintf("430 No such article");
	    return;
	}
	l = fgetheader(f, header);
	if (!l || !(*l)) {
	    nntpprintf("430 No such header: %s", header);
	    return;
	}
        STRIP_TRAILING_SPACE(l);

        if (mp) {
            ap = patterns;
            while (ap) {
                if (!ngmatch((const char*)&(ap->string), l))
                    break;
                ap = ap->next;
            }
            if (!ap) { /* doesn't match any pattern */
                nntpprintf("221 %s matches follow:", header);
		printf(".");
                fclose(f);
                return;
            }
        }

	nntpprintf("221 %s %s follow:", header, (mp ? "matches" : "headers"));
	printf("%s\r\n.\r\n", l ? l : "(none)");

        fclose(f);
        return;
    }

    if (!group) {
	nntpprintf("412 Use the GROUP command first");
	return;
    }

    markinterest();
    
    if (!chdirgroup(group->name, FALSE))
	pseudogroup = TRUE;

    if (!pseudogroup && (xovergroup != group)) {
	if (!getxover())
	    pseudogroup = TRUE;
	else
	    xovergroup = group;
    }

    if (pseudogroup) {
	n = 8;		/* also match "Newsgroups" */
        if (mp) {	/* placeholder matches never */
            nntpprintf("221 %s header matches follow:", header);
	    nntpprintf(".");
            return;
        }
	do {
	    n--;
	} while (n >= 0 && strncasecmp(h[n], header, strlen(h[n])) != 0) ;
	if (n < 0) {
	    nntpprintf("430 No such header: %s", header);
	    return;
	}
	nntpprintf("221 First line of %s pseudo-header follows:", header);
	if (n == 0)		/* Subject: */
	    printf("1 Leafnode placeholder for group %s\r\n", group->name);
	else if (n == 1)	/* From: */
	    printf("1 Leafnode <nobody@%s>\r\n", fqdn);
	else if (n == 2)	/* Date: */
	    printf("1 %s\r\n", rfctime());
	else if (n == 3)	/* Message-ID: */
	    printf("1 <leafnode:placeholder:%s@%s>\r\n", group->name, fqdn);
	else if (n == 4)	/* References */
	    printf("1 (none)\r\n");
	else if (n == 5)	/* Bytes */
	    printf("1 %d\r\n", 1024);	/* just a guess */
	else if (n == 6)	/* Lines */
	    printf("1 %d\r\n", 22);		/* from buildpseudoart() */
	else if (n == 7)	/* Newsgroups */
	    printf("1 %s\r\n", group->name);
	printf(".\r\n");
	return;
    }

    if (messages && isdigit((unsigned char)*messages)) {
        /* handle range form */
        a = strtoul(messages, &l, 10);
	if (a < xfirst)
	    a = xfirst;
	if (a && l && *l) {
	    while (l && isspace((unsigned char)*l))
		l++;
	    if (*l == '-') {
	        dash = 1;
		b = xlast;
		l++;
		if (l && *l)
		    b = strtoul(l, &l, 10);
	    }
	    while (l && isspace((unsigned char)*l))
		l++;
	    if (l && *l) {
                /* Here we handle at least 2 commands, so be very
                   unspecific */
		nntpprintf("502 Syntax error");
		return;
	    }
	    if (dash && b > xlast)
		b = xlast;
	}
    } else {
	a = b = artno;
    }

    n = 7;	/* can't get Newsgroups: from .overview files */
    do {
        n--;
    } while (n > -1 && strncasecmp(header, h[n], strlen(h[n])));
    
    if (a < group->first)
	a = group->first;
    else if (a > group->last)
	a = group->last;

    if (b < a)
	b = a;
    else if (b > group->last)
	b = group->last;

    /* Check whether there are any articles in the range specified */
    i = -1;
    c = a;
    while ((c <= b) && (i == -1)) {
	i = findxover(c);
	c++;
    }
    if (i == -1) {
	nntpprintf("420 No article in specified range");
	return;
    }

    if (n >= 0) {
	nntpprintf("221 %s header %s(from overview) for postings %d-%d:",
		    h[n], mp ? "matches " : "", a, b);
	s[1023] = '\0';
	for(c=a; c<=b; c++) {
	    if (xoverinfo &&
		 c >= xfirst && c <= xlast &&
		 (i=findxover(c)) >= 0) {
		char * l = xoverinfo[i].text;
		int d;
		for(d=0; l && d<=n; d++)
		    l = strchr(l+1, '\t');
		if (l) {
		    char * p;
		    strncpy(s, ++l, 1023);
		    p = strchr(s, '\t');
		    if (p)
			*p = '\0';
		}
                if (mp) {
                    ap = patterns;
                    while (ap) {
                        if (!ngmatch((const char*)&(ap->string), s))
                            break;
                        ap = ap->next;
                    }
                    if (!ap)
                        continue;
                }
                printf("%lu %s\r\n", c, strlen(s) ? s : "(none)");
	    }
	}
    } else {
	nntpprintf("221 %s header %s(from article files) for postings %lu-%lu:",
		   header, mp ? "matches " : "", a, b);
	for(c=a; c<=b; c++) {
	    sprintf(s, "%lu", c);
	    l = getheader(s, header);
/*
	    STRIP_TRAILING_SPACE(l);
*/
            if (mp) {
		ap = patterns;
                while (ap) {
                    if (!ngmatch((const char*)&(ap->string), l))
                        break;
                    ap = ap->next;
                }
                if (!ap)
                    continue;
            }
            printf("%lu %s\r\n", c, (l && *l) ? l : "(none)");
	}
    }

    printf(".\r\n");
    return;
}

#undef STRIP_TRAILING_SPACE

void doxover(const char *arg) {
    char * l;
    unsigned long a, b, art;
    long int index;
    int flag = FALSE;

    if (!group) {
	nntpprintf("412 Use the GROUP command first");
	return;
    }
    markinterest();

    l = NULL;
    b = a = strtoul(arg, &l, 10);
    if (a && l && *l) {
	while (l && isspace((unsigned char)*l))
	    l++;
	if (*l=='-')
	    b = strtoul(++l, &l, 10);
	while (l && isspace((unsigned char)*l))
	    l++;
	if (l && *l) {
	    nntpprintf("502 Usage: XOVER first[-last]");
	    return;
	}
    }

    if (!chdirgroup(group->name, FALSE))
	pseudogroup = TRUE;

    if (!pseudogroup) {
	if (xovergroup != group && getxover())
	    xovergroup = group;

	/* no arguments at all */
	if (!arg || !strlen(arg)) {
	    a = xfirst;
	    b = xfirst;
	}
	if (b == 0)
	    b = xlast;
	if ((a > xlast) || (b < xfirst)) {
	    nntpprintf("420 No articles in specified range.");
	    return;
	}
	if (b > xlast)
	    b = xlast;
	if (a < xfirst)
	    a = xfirst;

	for(art=a; art<=b; art++) {
	    index = findxover(art);
	    if (index >= 0 && xoverinfo[index].text != NULL) {
	        if (!flag) {
		    flag = TRUE;
		    nntpprintf("224 Overview information for postings %lu-%lu:",
				a, b);
		}
		printf("%s\r\n", xoverinfo[index].text);
	    }
	}
	if (flag)
	    printf(".\r\n");
	else
	    nntpprintf("420 No articles in specified range.");
    } else {
	if ((b < 1) || (a > 1) || (a > b)) {
	    nntpprintf("420 No articles in specified range.");
	    return;
	}

	nntpprintf("224 Overview information (pseudo) for postings 1-1:");
	nntpprintf("%d\t"
		    "Leafnode placeholder for group %s\t"
		    "nobody@%s (Leafnode)\t%s\t"
		    "<leafnode.%s@%s>\t\t1000\t40",
		    b, group->name, fqdn, rfctime(), group->name, fqdn);
	printf(".\r\n");
    }
}



void dolistgroup(const char * arg) {
    struct newsgroup * g;
    long int index;
    unsigned long art;

    if (arg && strlen(arg)) {
	g = findgroup(arg);
	if (!g) {
	    nntpprintf("411 No such group: %s", arg);
	    return;
	} else {
	    group = g;
	    artno = g->first;
	}   
    } else if (group) {
	g = group;
    } else {
	nntpprintf("412 No group specified");
	return;
    }
    
    markinterest();
    group = g;
    if (!chdirgroup(g->name, FALSE))
	pseudogroup = TRUE;
    else if ((xovergroup != group) && !getxover())
	pseudogroup = TRUE;
    else {
	pseudogroup = FALSE;
	xovergroup = group;
    }

    if (pseudogroup) {
	nntpprintf("211 Article list for %s follows (pseudo)", g->name);
	printf("%lu \r\n", g->last ? g->last : 1);
    } else {
	nntpprintf("211 Article list for %s (%lu-%lu) follows",
		    g->name, xfirst, xlast);
	for(art=xfirst; art<=xlast; art++) {
	    index = findxover(art);
	    if (index >= 0 && xoverinfo[index].text)
		printf("%lu \r\n", art);
	}
    }
    printf(".\r\n");
}

/*
 * authinfo stuff
 */

/*
 * read users and their crypted passwords into a stringlist
 */
static int readpasswd(void) {
    char *l;
    FILE *f;
    int error;
    struct stringlist * ptr = NULL;

    sprintf(s, "%s/users", libdir);
    if ((f = fopen(s, "r")) == NULL) {
	error = errno;
	ln_log(LNLOG_ERR, "unable to open %s: %s", s, strerror(errno));
	return error;
    }
    while ((l = getaline(f)) != NULL) {
	appendtolist(&users, &ptr, l);
    }
    fclose(f);
    return 0;
}

int isauthorized(void) {
    if (!authentication)
	return TRUE;
    if (authflag)
	return TRUE;
    nntpprintf("480 Authentication required for command");
    return FALSE;
}

void doauthinfo(const char *arg) {
    char cmd[MAXLINELENGTH] = "";
    char param[MAXLINELENGTH] = "";
#ifdef TODO
    char salt[3] = { 0, 0, 0 };
#endif
    static char * user = NULL;
    char * p;
    int result = 0;

    if (!arg || !strlen(arg))
	result = P_SYNTAX_ERROR;
    else if (!authentication)
	result = P_NOT_SUPPORTED;
    else if (sscanf(arg, "%s %s", cmd, param) != 2)
	result = P_SYNTAX_ERROR;
    else if (!strcasecmp(cmd, "user")) {
	if (authentication == AM_NAME) {
	    if (getpwnam(param))
		result = P_ACCEPTED;
	    else
		result = P_REJECTED;
	}
	else if (authentication == AM_FILE) {
	    user = strdup(param);
	    result = P_CONTINUE;
	}
    }
    else if (!strcasecmp(cmd, "pass")) {
	if (authentication == AM_NAME)
	    result = P_SYNTAX_ERROR;
	else if (authentication == AM_FILE) {
	    if ((p = findinlist(users, user)) != NULL) {
#ifdef TODO
	/* for some reason the line returned by findinlist() cannot be
	   parsed by the following code
	 */
	        char *q, *r = NULL;

		q = strdup(p);
		r = q;
		while (r && *r && !isspace((unsigned char *)r)) {
		    r++;
		    ln_log(LNLOG_DEBUG, "%s", r);
		}
		salt[0] = passwd[0];
		salt[1] = passwd[1];
		p = crypt(param, salt);
		if (!strcmp(r, p))
		    result = P_ACCEPTED;
		else
		    result = P_REJECTED;
		free(q);
#endif
	    }
	    else
	        result = P_REJECTED;
	}
    }
    else
	result = P_SYNTAX_ERROR;

    switch (result) {
	case P_ACCEPTED: {
	    nntpprintf("%d Authentication accepted", result);
	    authflag = 1;
	    break;
	}
	case P_CONTINUE: {
	    nntpprintf("%d More authentication required", result);
	    break;
	}
	case P_REJECTED: {
	    nntpprintf("%d Authentication rejected", result);
	    authflag = 0;
	    break;
	}
	case P_SYNTAX_ERROR: {
	    nntpprintf("%d Authentication method not supported", result);
	    break;
	}
	case P_NOT_SUPPORTED: {
	    nntpprintf("%d Authentication not supported", result);
	    break;
	}
	default: {
	    nntpprintf("%d This should not happen: %d",
			P_SYNTAX_ERROR, result);
	    break;
	}
    }	/* switch */
}

int main(int argc, char ** argv) {
    int option, reply;
/* GNU libc5 does not have socklen_t */
/*    socklen_t fodder; */
    int fodder;
    char *conffile;
    char peername[256];
    struct hostent *he;
    FILE *se;
#ifdef HAVE_IPV6
    struct sockaddr_in6 sa;
    struct sockaddr_in6 peer;
#else
    struct sockaddr_in sa;
    struct sockaddr_in peer;
#endif

    conffile = critmalloc(strlen(libdir) + 10,
			   "Allocating space for configuration file name");
    sprintf(conffile, "%s/config", libdir);

    if (!initvars(NULL))
	exit(EXIT_FAILURE);

    ln_log_open("leafnode");

    /* have stderr discarded */
    se=freopen("/dev/null", "w+", stderr);
    if(!se) {
	ln_log_so(LNLOG_ERR, "503 Failure: cannot open /dev/null: %s",
	       strerror(errno));
	exit(EXIT_FAILURE);
    }     

    while ((option=getopt(argc, argv, "F:DV")) != -1) {
	if (!parseopt("leafnode", option, optarg, conffile)) {
	    ln_log(LNLOG_WARNING, "Unknown option %c", option);
	    exit(EXIT_FAILURE);
	}
	debug = debugmode;
	verbose = 0;	/* overwrite verbose logging */
    }

    if ((reply = readconfig(conffile)) != 0) {
	ln_log_so(LNLOG_ERR, "503 Unable to read configuration from %s: %s",
		conffile, strerror(reply));
	exit(EXIT_FAILURE);
    }

    if (owndn) {
	strncpy(fqdn, owndn, FQDN_SIZE-1);
    } else {
	fodder = sizeof(sa);
	if (getsockname(0, (struct sockaddr *)&sa, &fodder)) {
	    if(errno != ENOTSOCK)
		ln_log(LNLOG_NOTICE, "cannot getsockname: %s", strerror(errno));
	    /* FIXME: bail out? */
	    strcpy(fqdn, "localhost");
	} else {
#ifdef HAVE_IPV6
	    he = gethostbyaddr((char *)&sa.sin6_addr,
				sizeof(sa.sin6_addr),
				AF_INET6);
#else
	    he = gethostbyaddr((char *)&sa.sin_addr.s_addr,
				sizeof(sa.sin_addr.s_addr),
				AF_INET);
#endif
	    if(!he) ln_log(LNLOG_NOTICE, "cannot gethostbyaddr: %d",
			   h_errno);
#ifdef HAVE_IPV6
	    inet_ntop(AF_INET6, &sa.sin6_addr, peername, sizeof(peername));
#else
	    inet_ntop(AF_INET, &sa.sin_addr, peername, sizeof(peername));
#endif

	    strncpy(fqdn, he && he->h_name ? he->h_name : peername, 
		    FQDN_SIZE-1); }
    }
    if (!strcasecmp(fqdn, "localhost"))
	whoami();

    artno = 0;
    verbose = 0;
    umask((mode_t)02);

    fodder=sizeof(peer);
    if (getpeername(0, (struct sockaddr *)&peer, &fodder)) {
	if (errno != ENOTSOCK) {
	    ln_log(LNLOG_ERR, "Cannot getpeername: %s, aborting.", 
		       strerror(errno));
	    exit(1);
	}
    } else {
	ln_log(LNLOG_INFO, "Connect from %s", 
#ifdef HAVE_IPV6
               inet_ntop(AF_INET6, &peer.sin6_addr, 
			 peername, sizeof(peername))
#else
	       inet_ntop(AF_INET, &peer.sin_addr,
			 peername, sizeof(peername))
#endif
	   );
    }
/* #endif */

    if (authentication && (reply = readpasswd()) > 0) {
	printf("503 Unable to read user list, exiting (%s)\r\n",
		strerror(reply));
	ln_log(LNLOG_NOTICE, ">503 Unable to read user list, exiting (%s)",
		strerror(reply));
	exit(EXIT_FAILURE);
    }

    printf("200 Leafnode NNTP Daemon, version %s running at %s\r\n", 
	   version, fqdn);
    fflush(stdout);
 
    pseudogroup = FALSE;

    rereadactive();
    readlocalgroups();
    
    signal(SIGCHLD, SIG_IGN); 

    gmt_off = gmtoff();	/* get difference between local time and GMT */
    
    parser();
    fflush(stdout);

    exit(0);
}
