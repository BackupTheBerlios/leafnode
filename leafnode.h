/* $Id: leafnode.h,v 1.39 2002/04/28 10:40:31 ralf Exp $ */
#ifndef LEAFNODE_H
#define LEAFNODE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#error "HAVE_CONFIG_H is undefined. This condition is unsupported."
#endif

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

#if !HAVE_WORKING_FORK
#define fork() ((pid_t)(-1))
#endif

/* work around splint not knowing the correct semantics for pcre and other stuff */
#ifdef __LCLINT__
#include "lclint_fixes.h"
#else /* not __LCLINT__ */
#include <pcre.h>
#endif /* not __LCLINT__ */


    typedef int bool;

    /*@constant int BLOCKSIZE;@*/
#define BLOCKSIZE 16384

    /*@constant mode_t MKDIR_MODE;@*/
#define MKDIR_MODE 0750

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
			(p)++; \

/* skip first word, replace its trailing space by nuls */
#define CUTSKIPWORD(p) while (*(p) && !isspace((unsigned char) *(p))) \
			(p)++; \
		    while (*(p) && isspace((unsigned char) *(p))) { \
			*(p)++ = '\0'; \
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
    int initvars(const char *const progname);

/* get configuration file */
/*@null@*//*@observer@*/ char *
     getoptarg(char option, int argc, char *argv[]);
    int findopt(char option, int argc, char *argv[]);

/* conffile is changed */
    int parseopt(const char *, int, /*@null@*/ const char *, /*@unique@*/ char *conffile,
		 size_t);

/* converts a message-id to a file name, the return value points to static storage  */
    /*@dependent@*/ char *lookup(/*@null@*/ const char *msgid);

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
#define TOKENSIZE 80		/* needed for parsing */
/* changes(and optionally creates) directory */
    int chdirgroup(const char *group, int creatdir);

/* is the group an interesting one? */
    int is_interesting(const char *groupname);
    void checkinteresting(void);

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

    void insertgroup(const char *name, const char status, long unsigned first,
		     long unsigned last, time_t date, const char *desc)
	/*@modifies internalState@*/ ;
    void changegroupdesc(const char *groupname, char *desc);
    void mergegroups(void)
	/*@globals active@*/
	/*@modifies active, internalState@*/ ;
    /*@null@*/ /*@dependent@*/ struct newsgroup *findgroup(const char *name, struct newsgroup *a,
				size_t asize);	/* active must be read */
    time_t query_active_mtime(void);
    void rereadactive(void);	/* only reread if none read or if it has changed */
    int writeactive(void);
    void freeactive(/*@null@*/ /*@only@*/ struct newsgroup *a);
    void mergeactives(struct newsgroup *old, struct newsgroup *new) ;
    struct newsgroup *cpactive(struct newsgroup *a);
/*
 * local groups
 */
    void insertlocal(const char *name);
    void readlocalgroups(void);
    int is_localgroup(const char *groupname);
    int is_alllocal(const char *grouplist);
    void freelocal(void);

/* translation from message-id to article number, used in fetch and expire */
    void clearidtree(void);
    void insertmsgid(const char *msgid, unsigned long art);
    unsigned long findmsgid(const char *msgid);

/* -----------here starts the new stuff-----------------*/
/*
 * a linear list of strings
 */
    struct stringlist {
	struct stringlist *next;
	char string[1];
    };
    void appendtolist(struct stringlist **list, struct stringlist **lastentry,
		      /*@unique@*/ char *newentry);
    /* append "newentry" to "list". "lastentry" points to the last
       entry in "list" and must be supplied. */
    /*@dependent@*//*@null@*/ char *findinlist(/*@null@*/ struct stringlist *haystack,
					       /*@null@*/ const char * const needle);

    /* find a string in a stringlist by doing a linear search */
    void freelist(/*@null@*/ /*@only@*/ struct stringlist *list);

    /* free memory occupied by a stringlist */
    int stringlistlen(/*@null@*/ const struct stringlist *list);
    /*@null@*/ /*@only@*/ struct stringlist *cmdlinetolist(const char *cmdline);

    /* convert a space separated string into a stringlist */
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
	char *newsgroup;
	long limit;
	char *cleartext;
	pcre *expr;
	char *action;
    };
    struct filterlist {
	struct filterentry *entry;
	struct filterlist *next;
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
    int store(const char *filename, int, /*@null@*/ const struct filterlist *);
    int store_stream(FILE * stream, int, /*@null@*/ const struct filterlist *,
		     ssize_t);
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
     mgetheader( /*@notnull@*/ const char *hdr, /*@null@*/ char *buf);

/*
 * various functions in artutil.c
 */
    void supersede_cancel(const char *msgid, const char *, const char *);

/*
 * xover stuff -- for nntpd.c
 */
    struct xoverinfo {
	unsigned long artno;
	char *text;
	int exists;
    };

    enum xoverfields {
	XO_ARTNO = 1,
	XO_SUBJECT,
	XO_FROM,
	XO_DATE,
	XO_MESSAGEID,
	XO_REFERENCES,
	XO_BYTES,
	XO_LINES,
	XO_XHDR
    };

    extern struct xoverinfo *xoverinfo;
    extern unsigned long xfirst;
    extern unsigned long xlast;
    long findxover(unsigned long article);

    /* find index number for an article, return -1 on error */
    int maybegetxover(/*@null@*/ struct newsgroup *g);
	/* set xoverinfo, return 0 on error, nonzero else, fill in water marks */
    int xgetxover(const int, /*@null@*/ struct newsgroup *g);
	/* set xoverinfo, return 0 on error, nonzero else, fill in water marks */
    int getxover(const int);	/* set xoverinfo, return 0 on error, nonzero else */
    void fixxover(void);	/* repair all .overview files */
    void gfixxover(const char *g);	/* repair .overview in groups g */
    void freexover(void);	/* free xoverinfo structure */

/*
 * the strings in config.c
 */
    extern const char *spooldir;
    extern const char *libdir;
    extern const char *bindir;
    extern const char *version;
    extern const char *lockfile;
    extern const char *compileinfo;
    extern const char *GZIP;
    extern const char *BZIP2;
    extern const char *CAT;
    extern const char *DEFAULTMTA;

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

    struct serverlist {
	/*@null@*/ struct serverlist *next;
	char *name;		/* Servername */
	char *username;
	char *password;
	int dontpost;		/* bool: this server is never fed articles */
	unsigned short port;	/* port, if 0, look up nntp/tcp */
	int usexhdr;		/* use XHDR instead of XOVER if sensible */
	int descriptions;	/* download descriptions as well */
	int timeout;		/* timeout in seconds before we give up */
	char active;
    };

    extern /*@null@*/ struct expire_entry *expire_base;

    extern char *mta;		/* mail transfer agent for mailing to moderators */
    /* expire for certain groups */
    extern unsigned long artlimit;

    /* max # of articles to read per group in one go */
    extern unsigned long initiallimit;

    /* max # of articles to read at first time */
    extern int delaybody;	/* delay download of message body */
    extern int debugmode;	/* log lots of stuff via syslog */
    extern long windowsize;
/* Note: Sync the DEBUG_ flags below with config.example */
#define DEBUG_MISC 1
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
    extern int timeout_active;	/* reread active file after that many days */
    extern int timeout_client;	/* activity timeout for clients in seconds */
    extern int filtermode;	/* can be one of */
#define FM_NONE  0
#define FM_XOVER 1
#define FM_HEAD  2
#define FM_BOTH  FM_XOVER|FM_HEAD
    extern /*@null@*/ char *filterfile;	/* filename where filter resides */
    extern /*@null@*/ char *pseudofile;	/* filename of pseudoarticle body */
    extern /*@null@*/ char *owndn;	/* own domain name, if you can't set one */
    extern /*@null@*/ struct serverlist *servers;
    extern int killbogus;	/* kill bogus files in newsgroups */

    void freeconfig(void);

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
    extern int debug;		/* debug level */

/*
 * misc prototypes
 */
    /*@falsewhennull@*/ int ihave(/*@null@*/ const char *mid);
    int lockfile_exists(int block);
    void putaline(FILE *, const char *fmt, ...)
	__attribute__ ((format(printf, 2, 3)));
    extern char last_command[1025];
    void readexpire(void);
    void free_expire(void);
    int readconfig(char *configfile);
    void whoami(void);
    void lowercase(char *string);
    int ngmatch(const char *pattern, const char *string);
    int copyfile(FILE * infile, FILE * outfile, long n);

    int initinteresting(void);
    /*@null@*/ /*@only@*/ RBLIST *openinteresting(void);
    /*@null@*/ /*@owned@*/ const char *readinteresting(/*@null@*/ RBLIST *);
    void closeinteresting(/*@null@*/ /*@only@*/ RBLIST *);
    void freeinteresting(void);


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
    bool authenticate(void);	/* authenticate ourselves at a server */
    int nntpreply(void);	/* decode an NNTP reply number */
    int newnntpreply(/*@null@*/ /*@out@*/ char **);	/* decode an NNTP reply number */
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
    int log_unlink(const char *);
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

/* mailto.c */
    int mailto(const char *address, int fd);

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

#ifdef __cplusplus
}
#endif
#endif				/* #ifndef LEAFNODE_H */
