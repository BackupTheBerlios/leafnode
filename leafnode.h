/* $Id: leafnode.h,v 1.104 2004/08/19 01:48:46 emma Exp $ */
#ifndef LEAFNODE_H
#define LEAFNODE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#error "HAVE_CONFIG_H is undefined. This condition is unsupported."
#endif

#define _GNU_SOURCE 1

#include "attributes.h"
#include "redblack.h"

/* I wish the world were a happy place */
#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

#define COUNT_OF(a) (sizeof(a)/sizeof((a)[0]))

/* limits.h is supposed to contain PATH_MAX, we include sys/param.h too */
#include <limits.h>
#include <sys/param.h>
#include <unistd.h>

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_AP_CONFIG_H
#define AP_CONFIG_H
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#include <sys/errno.h>
#else
extern int errno;
#endif				/* HAVE_ERRNO_H */

#include <sys/types.h>		/* size_t */
#include <stdio.h>		/* FILE */
#include <time.h>		/* time_t */
#include <stdarg.h>		/* va_list */
#include <dirent.h>		/* DIR */

#ifndef HAVE_WORKING_FORK
#define fork() ((pid_t)(-1))
#endif

/* work around splint not knowing the correct semantics for pcre and other stuff */
#ifdef __LCLINT__
#include "lclint_fixes.h"
#else /* not __LCLINT__ */
#include <pcre.h>
#endif /* not __LCLINT__ */


#ifndef __cplusplus
typedef int bool;
#endif

/*@constant int LN_PATH_MAX;@*/
#define LN_PATH_MAX (PATH_MAX + 1)

/*@constant int BLOCKSIZE;@*/
#define BLOCKSIZE 16384

/*@constant mode_t MKDIR_MODE;@*/
#define MKDIR_MODE 0770

/*@constant unsigned long LOCKWAIT;@*/
/** how long to wait for a lock, in seconds */
#define LOCKWAIT 5UL

#define BASENAME(a) (strrchr((a), '/') ? strrchr((a), '/') : (a))
#define WHITESPACE " \t"

/* skip linear white space */
#define SKIPLWS(p) while (*(p) && isspace((unsigned char) *(p))) { (p)++; }

/* skip first word and its trailing space */
#define SKIPWORD(p) while (*(p) && !isspace((unsigned char) *(p))) \
(p)++; \
    SKIPLWS(p);

/* skip word, pointer on first space */
#define SKIPWORDNS(p) while (*(p) && !isspace((unsigned char) *(p))) \
(p)++;

/* skip first word, replace its trailing space by nuls */
#define CUTSKIPWORD(p) while (*(p) && !isspace((unsigned char) *(p))) \
(p)++; \
    while (*(p) && isspace((unsigned char) *(p))) { \
	*((p)++) = '\0'; \
    }

/* strip trailing isspace characters */
#define STRIP_TRAILING_SPACE(str) \
{ \
    if ((str) && *(str)) { \
	char *__tmp = (str) + strlen(str) - 1; \
	    while (__tmp >= str \
		    && isspace((unsigned char)*__tmp)) {\
		*__tmp-- = '\0'; \
	    } \
    } \
}

#ifndef HAVE_SOCKLEN_T
typedef unsigned int socklen_t;
#endif

#ifndef HAVE_SNPRINTF
int snprintf(char *str, size_t n, const char *format, ...);
#endif

#ifndef HAVE_VSNPRINTF
int vsnprintf(char *str, size_t n, const char *format, va_list ap);
#endif

#ifndef HAVE_MKSTEMP
int mkstemp(char *);
#endif

/*
 * end of actions due to autoconf
 */

/*
 * various constants
 */
#define SECONDS_PER_DAY (24 * 60 * 60)
#define FQDN_SIZE ((size_t)256)

/* local and canonical line separators */
#define LLS "\n"
#define CLS "\r\n"

/* name of .last.posting in newsgroup directory */
#define LASTPOSTING ".last.posting"

/* initialize global variables */
int initvars(const char *const progname, int logtostdout);
int init_post(void);
/*@noreturn@*/ void init_failed(const char *);

/* get configuration file */
/*@null@*//*@observer@*/ char *
getoptarg(char option, int argc, char *argv[]);
int findopt(char option, int argc, char *argv[]);

/* conffile is malloced */
int parseopt(const char *, int, /*@null@*/ const char *, /*@null@*/ char **);
#define GLOBALOPTS ":VveD:F:d:"

    /* handling of misc. lines */
    /*@null@*/ /*@dependent@*/ char *getaline(FILE * f);
    /* reads one line, regardless of length, returns pointer to static buffer */

    /*@null@*/ /*@only@*/ char *
    mygetfoldedline(const char *, unsigned long, FILE * f);
    /* reads one line, regardless of length, returns malloc()ed string! */
#define getfoldedline(a) mygetfoldedline(__FILE__,__LINE__,a)
    /*@null@*/ /*@dependent@*/ char *mgetaline(FILE * f);
    /*@null@*/ /*@dependent@*/ char *timeout_getaline(FILE * f, unsigned int seconds);

    int parse_line(/*@unique@*/ char *l, /*@out@*/ char *param, /*@out@*/ char *value);

    /* parse a line of form "param = value" */
#define TOKENSIZE 4096	/* needed for parsing */
    /* changes(and optionally creates) directory */
    int chdirgroup(const char *group, int creatdir);

    /* is the group an interesting one? */
    int is_interesting(const char *groupname);
    void expireinteresting(void);

    int is_dormant(const char *groupname);

    /*
     * newsgroup management
     */
    struct newsgroup {
	unsigned long first;
	unsigned long last;
	unsigned long count;	/* number of articles in the group */
	char *name;
	char *desc;
	time_t age;
	char status;		/* "y" if posting is permitted, "n" if not,
				   "m" for moderated.
				   "Other status strings may exist." */
    };

extern /*@null@*/ /*@owned@*/ struct newsgroup *active;
extern /*@null@*/ /*@owned@*/ struct newsgroup *oldactive;

void insertgroup(const char *name, const char status, long unsigned first,
	long unsigned last, time_t date, const char *desc)
/*@modifies internalState@*/ ;
void changegroupdesc(const char *groupname, char *desc);
void mergegroups(void)
    /*@globals active@*/
    /*@modifies active, internalState@*/ ;
    /*@null@*/ /*@dependent@*/ struct newsgroup *findgroup(const char *name,
	    struct newsgroup *a,
	    ssize_t asize);	/* active must be read */
time_t query_active_mtime(void);
void rereadactive(void);	/* only reread if none read or if it has changed */
int writeactive(void);
void freeactive(/*@null@*/ /*@only@*/ struct newsgroup *a);
void mergeactives(struct newsgroup *old, struct newsgroup *newng) ;
/*@null@*/ struct newsgroup *mvactive(/*@null@*/ struct newsgroup *a);
/*
 * local groups
 */
void insertlocal(const char *name);
void readlocalgroups(void);
int is_localgroup(const char *groupname);
int is_alllocal(const char *grouplist);
int is_anylocal(const char *grouplist);
void freelocal(void);

/* -----------here starts the new stuff-----------------*/
/**
 * a linear list of strings
 * warning: the list now has a dummy head/tail element,
 * which has a NULL next (or prev, depending on traversal direction)
 * field. So, if sln->next is NULL, do not handle sln!
 */
struct stringlistnode {
    struct stringlistnode *next;
    struct stringlistnode *prev;
    char string[1];
};

/* list header for stringlistnode - contains the merged dummy elements */
struct stringlisthead {
    struct stringlistnode *head;
    struct stringlistnode *tail;
    struct stringlistnode *tailpred;
};

void prependtolist(struct stringlisthead *list, /*@unique@*/ const char *newentry);
void appendtolist(struct stringlisthead *list, /*@unique@*/ const char *newentry);

/*@dependent@*//*@null@*/ char *findinlist(/*@null@*/ struct stringlisthead *haystack,
	/*@null@*/ const char * const needle);
/* find a string in a stringlist by doing a linear search */

/*@null@*//*@dependent@*/
void removefromlist(struct stringlistnode *f);
void freelist(/*@null@*/ /*@only@*/ struct stringlisthead *list);
bool is_listempty(const struct stringlisthead *list);
void initlist(struct stringlisthead **list);

/* free memory occupied by a stringlist */
int stringlistlen(/*@null@*/ const struct stringlisthead *list);

/* convert a space separated string into a stringlist */
/*@null@*/ /*@only@*/ struct stringlisthead *cmdlinetolist(const char *cmdline);

/* match str against patterns in a stringlist */
/*@null@*/ /*@dependent@*/ struct stringlistnode *
matchlist(struct stringlistnode *patterns, const char *str);

/*
 * filtering headers for regexp
 */
/*
 * a filterentry consists of the following fields:
 * newsgroup: pointer to the name of the newsgroup the filter should
 *            be applied to. Globs are allowed.
 * limit    : integer. If -1, use the regexp. Otherwise, apply this
 *	      limit to the parameter delineated in "cleartext".
 * cleartext: if isregexp == TRUE, a pointer to the cleartext of a regexp.
 *            if isregexp == FALSE, one of the following:
 *            maxage = [age in days]
 *            maxlines = [maximal number of lines]
 *            minlines = [minimal number of lines]
 *            maxbytes = [maximal number of bytes]
 *	      maxcrosspost = [maximal number of newsgroups]
 * expr:      if isregexp == TRUE, a pointer to the compiled regexp
 *            if isregexp == FALSE, NULL
 * action:    a pointer to one of the following:
 *            kill: article is discarded immediately, no further tests
 *		are done
 *            select: article is kept immediately, no further tests
 *		are done
 *	      header: article headers are retrieved immediately, no
 *		further tests are done
 *	      [positive or negative integer number]: number is added to
 * 		the score, further tests are performed
 */
struct filterentry {
    char *ngpcretext;
    pcre *newsgroups;
    int invertngs;
    long limit;
    char *cleartext;
    pcre *expr;
    char *action;
};
struct filterlist {
    struct filterlist *next;
    struct filterentry *entry;
};
extern /*@null@*/ /*@owned@*/ struct filterlist *filter;
/* all expressions precompiled */
int readfilter(/*@null@*/ const char *filterfile)
    /*@globals undef filter@*/ ;
    int killfilter(const struct filterlist *f, const char *hdr);
    struct filterlist *selectfilter(const char *groupname);
    void freefilter(/*@null@*/ /*@only@*/ struct filterlist *f);
    /* for selectfilter */
    void freeallfilter(/*@null@*/ /*@only@*/ struct filterlist *f);
    /* for deallocation */

    /*
     * store articles
     */
    int store(const char *filename, int, /*@null@*/ const struct filterlist *,
	    int);
int store_stream(FILE * stream, int, /*@null@*/ const struct filterlist *,
	ssize_t, int);
/*@observer@*/ const char *store_err(int);

/*
 * find a certain header in an article and return it
 */
/*@null@*//*@only@*/ char *
getheader( /*@notnull@*/ const char *filename, /*@notnull@*/
	const char *header);
/*@null@*//*@only@*/ char *
fgetheader( /*@null@*/ FILE * f, /*@notnull@*/ const char *header,
	int rewind_me);
/*@null@*//*@only@*/ char *
mgetheader( /*@notnull@*/ const char *hdr, /*@null@*/ const char *buf);

    /*
     * various functions in artutil.c
     */
    int xref_to_list(/*@exposed@*/ char *xref,
	    /*@out@*/ char ***groups, /*@null@*/ /*@out@*/ char ***artno_strings, int noskip);

void delete_article(const char *msgid, const char *, const char *);

/*
 * xover stuff -- for nntpd.c
 */
struct xoverinfo {
    unsigned long artno;
    char *text;
    int exists;
};

/*
 * XOVER entries.  DO NOT change ordering here without adapting
 * xoverentry in xoverutil.c as well!
 */
enum xoverfields {
    XO_ERR = -1,	/* error condition */
    XO_ARTNO = 0,
    XO_SUBJECT,
    XO_FROM,
    XO_DATE,
    XO_MESSAGEID,
    XO_REFERENCES,
    XO_BYTES,
    XO_LINES,
    XO_XREF
};

extern enum xoverfields matchxoverfield(const char *header);
/*@null@*/ /*@dependent@*/
char *getxoverfield(char *xoverline, enum xoverfields);
/*@null@*/ /*@only@*/ char * getxoverline(const int, const char *const);

extern struct xoverinfo *xoverinfo;
extern unsigned long xfirst;
extern unsigned long xlast;
extern unsigned long xcount;
long findxover(unsigned long article);
int findxoverrange(unsigned long low, unsigned long high,
	/*@out@*/ long *idxlow, /*@out@*/ long *idxhigh);

/* find index number for an article, return -1 on error */
int maybegetxover(/*@null@*/ struct newsgroup *g);
/* set xoverinfo, return 0 on error, nonzero else, fill in water marks */
int xgetxover(const int, /*@null@*/ struct newsgroup *g);
    /* set xoverinfo, return 0 on error, nonzero else, fill in water marks */
    int getxover(const int);	/* set xoverinfo, return 0 on error, nonzero else */
    void freexover(void);	/* free xoverinfo structure */
    extern int writexover(void);    /* write overview info */

    /*
     * the strings in config.c
     */
    extern char *spooldir;
    extern const char *def_spooldir;
    extern const char *sysconfdir;
    extern const char *bindir;
    extern const char *version;
    extern char *lockfile;
    extern const char *compileinfo;
    extern const char *GZIP;
    extern const char *BZIP2;
    extern const char *CAT;
    extern const char *DEFAULTMTA;
    extern const char *RUNAS_USER;

    /*
     * global variables from config file. These are defined in configutil.c
     */
    struct expire_entry {
	time_t xtime;
	struct expire_entry *next;
	char *group;
    };
extern long sendbuf;	/* TCP send buffer of currently connected server */
extern time_t default_expire;

time_t lookup_expire(char *group); /* expire_lookup.c */

enum feedtype { CPFT_NNTP = 0, CPFT_UUCP, CPFT_NONE };

struct serverlist {
    /*@null@*/ struct serverlist *next;
    char *name;		/* Servername */
    char *username;
    char *password;
    pcre *group_pcre;
    unsigned short port;	/* port, if 0, look up nntp/tcp */
    int usexhdr;		/* use XHDR instead of XOVER if sensible */
    int descriptions;	/* download descriptions as well */
    int noactive;		/* if true, do not request group lists */
    int noread;			/* if true, do not request articles */
    int timeout;		/* timeout in seconds before we give up */
    int post_anygroup;
    enum feedtype feedtype;
    char active;
};

/*@only@*/ struct serverlist *
create_server(/*@observer@*/ const char *name, unsigned short port);

extern /*@null@*/ struct expire_entry *expire_base;

struct delaybody_entry {
    struct delaybody_entry *next;
    char *group;
};

extern /*@null@*/ struct delaybody_entry *delaybody_base;

extern char *mta;		/* mail transfer agent for mailing to moderators */
/* expire for certain groups */
extern unsigned long artlimit;

/* max # of articles to read per group in one go */
extern unsigned long initiallimit;

/* max # of articles to read at first time */

extern int only_fetch_once;	/* do not query other servers for same groups */
extern int delaybody;	/* delay download of message body */
extern int debugmode;	/* log lots of stuff via syslog */
extern int no_direct_spool; /* if set, do not store remote posts locally */
extern long windowsize;
/* Note: Sync the DEBUG_ flags below with config.example */
#define DEBUG_LOGGING 1
#define DEBUG_IO   2
#define DEBUG_SORT 4
#define DEBUG_ACTIVE 8
#define DEBUG_CONFIG 16
#define DEBUG_FILTER 32
#define DEBUG_LOCKING 64
#define DEBUG_NNTP 128
#define DEBUG_EXPIRE 256
#define DEBUG_XOVER 512
#define DEBUG_CANCEL 1024
#define DEBUG_STORE 2048

extern int create_all_links;

/* store articles even in uninteresting groups */
extern int timeout_short;	/* don't fetch groups that have been

				   accidentally accessed after that many days */
extern int timeout_long;	/* don't fetch groups that have been accessed

				   that many days */
extern int authentication;	/* authentication method to use. Methods are: */

#define AM_FILE 1		/* authenticate password via
				   /etc/leafnode/users (needed for password
				   authentication) */
#define AM_PAM 2                /* authenticate using pam */
#define AM_MAX AM_PAM
extern int ln_log_posterip; /* log poster ip in
			       X-Leafnode-NNTP-Posting-Host header */
extern int timeout_active;	/* reread active file after that many days */
extern int timeout_client;	/* activity timeout for clients in seconds */
extern int filtermode;	/* can be one of */
#define FM_NONE  0
#define FM_XOVER 1
#define FM_HEAD  2
#define FM_BOTH  FM_XOVER|FM_HEAD
extern /*@null@*/ char *filterfile;	/* filename where filter resides */
extern /*@null@*/ char *localgroups; /* filename for local groups file */
extern /*@null@*/ char *pseudofile;	/* filename of pseudoarticle body */
extern /*@null@*/ char *owndn;	/* own domain name, if you can't set one */
extern /*@null@*/ struct serverlist *servers;

void freeconfig(void);
const char * get_feedtype(enum feedtype t);

/* list of servers to use */
/*
 * other global variables
 */
/* defined in nntputil.c */
extern /*@dependent@*/ FILE *nntpin;
extern /*@dependent@*/ FILE *nntpout;
extern int stat_is_evil;

/* defined in miscutil.c */
extern char fqdn[];		/* my name, and my naming myself */
extern int verbose;		/* verbosity level, for fetch and texpire */
void freegrouplist(/*@only@*/ struct rbtree *rb);
/*@null@*/ /*@only@*/ struct rbtree * initgrouplistdir(const char *dir, int);

/*@null@*/ /*@only@*/ struct rbtree *
initfilelist(FILE *f, const void *config,
	int (*comparison_function)(const void *, const void *, const void *));

/*
 * misc prototypes
 */
int attempt_lock(unsigned long timeout);
int handover_lock(pid_t pid);
void putaline(FILE *, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
extern char last_command[1025];
    void readexpire(void);
    void free_expire(void);
    int readconfig(/*@null@*/ const char *configfile);
    void lowercase(char *string);
    int ngmatch(const char *pattern, const char *string);
    int copyfile(FILE * infile, FILE * outfile, long n);

    int delaybody_group(/*@null@*/ const char *group);

    int initinteresting(void);
    /*@null@*/ /*@only@*/ RBLIST *openinteresting(void);
    /*@null@*/ /*@owned@*/ const char *readinteresting(/*@null@*/ RBLIST *);
    void closeinteresting(/*@null@*/ /*@only@*/ RBLIST *);
    void freeinteresting(void);
    /*@null@*/ /*@owned@*/ const char *
    addtointeresting(const char *key);


    int init_dormant(void);
    RBLIST *open_dormant(void);
    const char *read_dormant(RBLIST *);
    void close_dormant(RBLIST *);
    void free_dormant(void);


#if 0
    int rename(const char *oldname, const char *newname);
    /* to avoid barfing of Digital Unix */
#endif

    /*
     * stuff from nntputil.c
     */
    bool authenticate(const struct serverlist *s);	/* authenticate ourselves at a server */
    int nntpreply(const struct serverlist *s);	/* decode an NNTP reply number */
    int newnntpreply(const struct serverlist *s, /*@null@*/ /*@out@*/ char **);	/* decode an NNTP reply number */
    int nntpconnect(const struct serverlist *upstream);

/* connect to upstream server */
void nntpdisconnect(void);	/* disconnect from upstream server */
/*@dependent@*/ const char *rfctime(void); /* An rfc type date */

/* from strutil.c */
int check_allnum(const char *);	/* check if string is all made of digits */
int check_allnum_minus(const char *);	/* check if string is all made
					   of digits and "-" */

/*@null@*/ /*@only@*/ char *cuttab(const char *in, int field);
/* break tab-separated field */
int str_nsplit(/*@out@*/ char **array, const char *in, const char *sep, int maxelem);
/* Perl-like, split string into substrings */
void free_strlist(/*@null@*/ char **hdl /** string array to free */ );
/* free array of strings */
int str_isprefix(const char *string, const char *prefix);	/* strncasecmp variant */

/* from dirutil.c */
/* open directory, relative to spooldir, log problems */
/*@dependent@*//*@null@*/ DIR *open_spooldir(const char *);
/* read directory into a list of strings */
/*@null@*//*@only@*/ char **
dirlist(const char *name, int (*)(const char *),	/*@null@*/
	long unsigned *);
/* dito, prefixing the directory name */
/*@null@*//*@only@*/ char **
dirlist_prefix(const char *name, int (*)(const char *),
	/*@null@*/ long unsigned *);
/* dito, relative to the spool directory */
/*@null@*//*@only@*/ char **
spooldirlist_prefix(const char *name, int (*)(const char *),
	/*@null@*/ long unsigned *);
/* filters for dirlist */
int DIRLIST_ALL(const char *x);
int DIRLIST_NONDOT(const char *x);
int DIRLIST_ALLNUM(const char *x);

/* free string list */
void free_dirlist( /*@null@*//*@only@*/ char **);

/* from fopen_reg.c */
/*@null@*/ /*@dependent@*/ FILE *fopen_reg(const char *path, const char *m);

/* from nfswrite.c */
ssize_t nfswrite(int fd, const void *b, size_t count);
ssize_t nfswriteclose(int fd, const void *b, size_t count);

/* from tab2spc.c */
void tab2spc(char *);

/* from log_*.c */
int log_unlink(const char *, int);
int log_fsync(int);
int log_close(int);
int log_fclose(FILE *);
int log_rename(const char *, const char *);
int log_chmod(const char *, mode_t);
int log_fchmod(const int, mode_t);
int log_moveto(/*@notnull@*/ const char *, const char *dir);

/* from wildmat.c */
int wildmat(const char *text, const char *p);

/* from lockfile.c */
int safe_mkstemp(char *templ);

/* from queues.c */
int checkforpostings(void);
int checkincoming(void);
int feedincoming(void);

#ifndef HAVE_INET_NTOP
const char *inet_ntop(int af, const void *s, char *dst, int x);
#endif

/* sort.c */
void my_sort(void *base, size_t nmemb, size_t size,
	int (*compar) (const void *, const void *),
	const char *f, unsigned long l);

#define sort(a,b,c,d) my_sort(a,b,c,d,__FILE__,__LINE__)

#ifndef HAVE_MERGESORT
int mergesort(void *base, size_t nmemb, size_t size,
	int (*cmp) (const void *, const void *));
#endif

/* parserange.c */
int parserange(const char *, unsigned long *, unsigned long *);
#define RANGE_ERR      1
#define RANGE_HAVEFROM 2
#define RANGE_HAVETO   4

/* safe_link.c */
int safe_link(const char *from, const char *to);
/* sync_link.c */
int sync_link(const char *from, const char *to);
/* sync_dir.c */
int sync_dir(const char *name);
int sync_parent(const char *name);

/* writes.c */
ssize_t writes(int fd, const char *string);

/* gmtoff.c */
time_t gmtoff(const time_t);

/* moderated.c */
/*@null@*/ /*@only@*/ char *getmoderator(const char *group);
/*@null@*/ /*@only@*/ char *checkstatus(const char *groups, const char status);

/* getwatermarks.c */
int getwatermarks(unsigned long *, unsigned long *, unsigned long /*@null@*/ *);

/* touch.c */
int touch_truncate(const char *name);

extern void /*@exits@*/ internalerror(void);
#define internalerror() do { ln_log(LNLOG_SCRIT, LNLOG_CTOP, "internal error at %s:%d", __FILE__, __LINE__); abort(); } while(0)

/* cmp_firstcolumn.c */
int cmp_firstcolumn(const void *a, const void *b,
	/*@unused@*/ const void *c);

/* debugutil.c */
/* set this to 1 for debugging */
#if 1
#define D(a) (a)
#else
#define D(a)
#endif
void d_stop_mid(const char *mid);

#if 0
#warning "WARNING: do not disable fsync in production use"
#define fsync(a) (0)
#endif

#endif				/* #ifndef LEAFNODE_H */
