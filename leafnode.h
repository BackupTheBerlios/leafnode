/* $Id: leafnode.h,v 1.7 2001/01/02 18:34:35 emma Exp $ */

#ifndef LEAFNODE_H
#define LEAFNODE_H

/* I wish the world were a happy place */
#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

/* limits.h is supposed to contain PATH_MAX, we include sys/param.h too */
#include <limits.h>
#ifndef PATH_MAX
#include <sys/param.h>
#define PATH_MAX MAXPATHLEN
#endif

#define BLOCKSIZE 16384

#include "config.h"	/* FreeSGI barfs on #ifdef HAVE_CONFIG_H */

#ifdef HAVE_AP_CONFIG_H
#define AP_CONFIG_H
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#include <sys/errno.h>
#endif
#ifndef HAVE_ERRNO_H
extern int errno;
#endif	/* HAVE_ERRNO_H */

#ifndef HAVE_STRDUP
char * strdup (const char *);
#endif

#include <sys/types.h>	/* size_t */
#include <stdio.h>	/* FILE */
#include <time.h>	/* time_t */
#include <stdarg.h>	/* va_list */
#include <dirent.h>     /* DIR */

#ifndef HAVE_SNPRINTF
int snprintf(char *str, size_t n, const char *format, ...);
#endif

#ifndef HAVE_VSNPRINTF
int vsnprintf(char *str, size_t n, const char *format, va_list ap);
#endif

#ifdef HAVE_LIBPCRE
#include <pcre.h>
#else
#include "pcre/pcre.h"
#endif

#define SECONDS_PER_DAY (24 * 60 * 60)

/* initialize global variables */
int initvars(char *progname);
/* get configuration file */
char * getoptarg(char option, int argc, char * argv[]);
int findopt(char option, int argc, char *argv[]);
int parseopt(char *progname, char option, char * optarg, char *conffile);

/* converts a message-id to a file name, the return value points into
   a static array */
const char * lookup (const char *msgid);

/* handling of misc. lines */
char * getaline(FILE *f);	/* reads one line, regardless of length */
int parse_line (char *l, char * param, char * value) ;
				/* parse a line of form "param = value" */
#define TOKENSIZE 80		/* needed for parsing */

/* changes (and optionally creates) directory */
int chdirgroup(const char *group, int creatdir);

/* is the group an interesting one? */
int isinteresting(const char * groupname);
void checkinteresting(void);

/*
 * newsgroup management
 */
struct newsgroup {
    unsigned long first;
    unsigned long last;
    unsigned long count;	/* number of articles in the group */
    char * name;
    char * desc;
    time_t age;
    char status;	/* "y" if posting is permitted, "n" if not,
    			   "m" for moderated.
			   "Other status strings may exist." */
};

void insertgroup(const char * name, long unsigned first,
	long unsigned last, int date, char * desc);
void changegroupdesc(const char * groupname, char * desc);
void mergegroups(void);
struct newsgroup * findgroup(const char* name);
void readactive(void);
void writeactive(void);
void fakeactive(void);

/*
 * local groups
 */
void insertlocal(const char *name);
void readlocalgroups(void);
int  islocalgroup(const char *groupname);
int  islocal(const char *grouplist);

extern struct newsgroup * active;

/* translation from message-id to article number, used in fetch and expire */

void clearidtree(void);
void insertmsgid(const char * msgid, unsigned long art);
int findmsgid(const char* msgid);

/* -----------here starts the new stuff-----------------*/

/*
 * a linear list of strings
 */
struct stringlist {
    struct stringlist * next;
    char string[1] ;
};
void appendtolist(struct stringlist ** list, struct stringlist ** lastentry,
    char *newentry);
	/* append "newentry" to "list". "lastentry" points to the last
	   entry in "list" and must be supplied. */
char * findinlist(struct stringlist * haystack, char * needle) ;
	/* find a string in a stringlist by doing a linear search */
void freelist(struct stringlist * list) ;
	/* free memory occupied by a stringlist */
int stringlistlen(const struct stringlist *list);
struct stringlist * cmdlinetolist(const char *cmdline) ;
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
    char * newsgroup;
    int  limit;
    char * cleartext;
    pcre * expr;
    char * action;
};

struct filterlist {
    struct filterentry *entry;
    struct filterlist *next;
};

extern struct filterlist * filter;      /* all expressions precompiled */

int readfilter(char *filterfile) ;
int killfilter(struct filterlist *f, char *hdr) ;
struct filterlist * selectfilter (char * groupname);
void freefilter(struct filterlist *f);

/*
 * artutil -- handling article files
 */

/*
 * store articles
 */
void storearticle (char * filename, char * msgid, char * newsgroups) ;
void store(const char * filename, FILE * filehandle, char * newsgroups,
	    const char *msgid);

/*
 * find a certain header in an article and return it
 */
char * getheader(const char * filename, const char * header);
char * fgetheader(FILE *f, const char * header);
char *mgetheader (const char *hdr, char *buf) ;

/*
 * various functions in artutil.c
 */
void supersede(const char * msgid);

/*
 * xover stuff -- for nntpd.c
 */
struct xoverinfo {
    unsigned long artno;
    char * text;
    int exists;
};

enum xoverfields {
    XO_ARTNO=1,
    XO_SUBJECT,
    XO_FROM,
    XO_DATE,
    XO_MESSAGEID,
    XO_REFERENCES,
    XO_BYTES,
    XO_LINES,
    XO_XHDR 
}; 

extern struct xoverinfo * xoverinfo;
extern unsigned long xfirst;
extern unsigned long xlast;

long findxover(unsigned long article);
		  /* find index number for an article, return -1 on error */
int getxover(void);	  /* set xoverinfo, return 0 on error, nonzero else */
void fixxover(void); 	  /* repair all .overview files */
void gfixxover(char * g); /* repair .overview in groups g */
void freexover(void);     /* free xoverinfo structure */

/*
 * the strings in config.c
 */
extern const char * spooldir;
extern const char * libdir;
extern const char * bindir;
extern const char * version;
extern const char * lockfile;

/*
 * global variables from config file. These are defined in configutil.c
 */
struct expire_entry {
    int xtime;
    struct expire_entry *next;
    char * group;
};

struct serverlist {
    int port ;
    int descriptions ;		/* download descriptions as well */
    int timeout ;		/* timeout in seconds before we give up */
    struct serverlist * next;
    char * name ;		/* Servername */
    char * username ;
    char * password ;
    char active;
};

extern time_t expire;	/* articles not touched since this time get deleted */
extern struct expire_entry * expire_base;
			/* expire for certain groups */
extern unsigned long artlimit;
			/* max # of articles to read per group in one go */
extern unsigned long initiallimit;
			/* max # of articles to read at first time */
extern int delaybody;	/* delay download of message body */
extern int avoidxover;  /* prefer XHDR over XOVER */
extern int debugmode;	/* log lots of stuff via syslog */
extern int create_all_links;
			/* store articles even in uninteresting groups */
extern int timeout_short;	/* don't fetch groups that have been
				   accidentally accessed after that many days */
extern int timeout_long;	/* don't fetch groups that have been accessed
				   that many days */
extern int authentication;	/* authentication method to use. Methods are: */
#define AM_NAME 1		/* authenticate only username via /etc/passwd */
#define AM_FILE 2		/* authenticate password via
				   /etc/leafnode/users (needed for password
				   authentication) */
extern int timeout_active;	/* reread active file after that many days */
extern int filtermode;		/* can be one of */
#define FM_NONE  0
#define FM_XOVER 1
#define FM_HEAD  2
#define FM_BOTH  3
extern char * filterfile;	/* filename where filter resides */
extern char * pseudofile;	/* filename of pseudoarticle body */
extern char * owndn;		/* own domain name, if you can't set one */
extern struct serverlist * servers;
				/* list of servers to use */

/*
 * other global variables
 */

/* defined in nntputil.c */
extern FILE *nntpin;
extern FILE *nntpout;

#define FQDN_SIZE 256
extern char s[];
extern char fqdn[];		/* my name, and my naming myself */

extern int verbose;		/* verbosity level, for fetch and texpire */
extern int debug;		/* debug level */

/*
 * misc prototypes
 */
int lockfile_exists(int silent, int block);
void putaline(const char *fmt, ...);
void retry(void);
void readexpire(void);
void free_expire(void);
int readconfig(char * configfile);
void whoami(void);
void lowercase(char *string);
int ngmatch(const char* pattern, const char* string);
void copyfile(FILE * infile, FILE * outfile, long n);

int rename(const char *oldname, const char *newname);
				/* to avoid barfing of Digital Unix */

/*
 * stuff from nntputil.c
 */
int authenticate(void);	/* authenticate ourselves at a server */
int nntpreply(void);		/* decode an NNTP reply number */
int nntpconnect(const struct serverlist * upstream);
				/* connect to upstream server */
void nntpdisconnect(void);	/* disconnect from upstream server */

const char* rfctime(void);	/* An rfc type date */

/* from strutil.c */
int check_allnum_minus(const char *); /* check if string is all made 
					 of digits and "-" */
char *cuttab(const char *in, int field); /* break tab-separated field */


/* from dirutil.c */
/* open directory, log problems */
DIR *log_open_dir(const char *); 
/* open directory, relative to spooldir, log problems */
DIR *log_open_spool_dir(const char *); 

#ifndef HAVE_INET_NTOP
const char *inet_ntop(int af, const void *s, char *dst, int x);
#endif

#endif	/* #ifndef LEAFNODE_H */
