/** \file miscutil.c
 * miscellaneous stuff.
 *
 * See AUTHORS for copyright holders and contributors.
 * See file COPYING for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "critmem.h"
#include "ugid.h"
#include "ln_log.h"
#include "format.h"
#include "msgid.h"
#include "mastring.h"
#include "get.h"
#include "redblack.h"
#include "validatefqdn.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#ifndef HAVE_SNPRINTF
#include <stdarg.h>
#endif

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static void whoami(void);

char fqdn[FQDN_SIZE];
extern struct state _res;

/*
 * global variables modified here
 */
int debugmode = 0;
int verbose = 0;

struct mydir {
/*@null@*//*@observer@*/ const char *name;
    mode_t m;
};

/*@+matchanyintegral@*/
static const struct mydir dirs[] = {
    {"failed.postings", 0755},	/* carries articles that could not be posted to upstream */
    {"interesting.groups", 0755},
    {"dormant.groups", 0755},
    {"leaf.node", 0755},
    {"message.id", 02755},
    {"out.going", 02755},	/* FIXME: outgoing queue */
    {"in.coming", 02755},	/* carries articles that must be stored */
    {"temp.files", 0700},	/* for temporary files */
    {0, 0}
};
/*@=matchanyintegral@*/

/*
 * initialize all global variables
 */
/*@-globstate@*/
int
initvars(const char *const progname, int logtostdout)
{
#ifndef TESTMODE
    uid_t ui;
    gid_t gi;
#endif /* not TESTMODE */
    struct mydir const *md = &dirs[0];

    /*@-noeffect@*/
    (void)progname;		/* shut up compiler warnings */
    /*@=noeffect@*/

#ifndef TESTMODE
    if (uid_getbyuname(RUNAS_USER, &ui)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot uid_getbyuname(%s,&ui): %m\n",
		RUNAS_USER);
	return FALSE;
    }
    if (ui == (uid_t)0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot run leafnode with user %s that has uid #0!\n",
		RUNAS_USER);
	return FALSE;
    }

    if (gid_getbyuname(RUNAS_USER, &gi)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot gid_getbyuname(%s,&gi): %m\n",
		RUNAS_USER);
	return FALSE;
    }
    if (gi == (gid_t)0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot run leafnode with user %s that has gid #0!\n",
		RUNAS_USER);
	return FALSE;
    }
#endif /* not TESTMODE */

    whoami();
    validatefqdn(logtostdout);

    /* config.c stuff does not have to be initialized */
    expire_base = NULL;
    /* These directories should exist anyway */
    while (md->name) {
	mastr *x = mastr_new(LN_PATH_MAX);

	mastr_vcat(x, spooldir, "/", md->name, NULL);
	if (mkdir(mastr_str(x), (mode_t) 0110)) {
		if (errno != EEXIST) {
			ln_log(LNLOG_SERR, LNLOG_CTOP,
					"cannot mkdir(%s): %m\n", mastr_str(x));
			mastr_delete(x);
			return FALSE;
		}
	} else {
#ifndef TESTMODE
		if (chown(mastr_str(x), ui, gi)) {	/* Flawfinder: ignore */
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			    "cannot chown(%s,%ld,%ld): %m\n",
			    mastr_str(x), (long)ui, (long)gi);
		    mastr_delete(x);
		    return FALSE;
		}
#endif /* not TESTMODE */
		if (log_chmod(mastr_str(x), md->m)) {
		    mastr_delete(x);
		    return FALSE;
		}
	}
	md++;
	mastr_delete(x);
    }
#ifndef TESTMODE
    if (gid_ensure(gi)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot ensure gid %ld: %m\n", (long)gi);
	return FALSE;
    }
    if (uid_ensure(ui)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot ensure uid %ld: %m\n", (long)ui);
	return FALSE;
    }
#endif /* not TESTMODE */

    if (chdir(spooldir)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot change to spooldir %s: %m\n",
		spooldir);
	return FALSE;
    }
    
    return TRUE;
}

/*@=globstate@*/

/*
 * Find whether a certain option is specified on the command line
 * and return the pointer to this option - i.e. if 0 is returned,
 * the option is not present. Does not call getopt().
 */
int
findopt(char option, int argc, char *argv[])
{
    int i, j;

    i = 1;
    while (i < argc) {
	if (strcmp(argv[i], "--") == 0) {	/* end of option args */
	    return 0;
	}
	if (argv[i][0] == '-') {	/* option found */
	    j = strlen(argv[i]) - 1;
	    while (j > 0) {
		if (argv[i][j] == option)
		    return i;
		j--;
	    }
	}
	i++;
    }
    return 0;
}

/*
 * Get configuration file from command line without calling getopt()
 * returns name of configuration file or NULL if no such file was found.
 * If there is more than one "-F configfile" present, use the first one.
 */
/*@null@*//*@observer@*/ char *
getoptarg(char option, int argc, char *argv[])
{
    int i, j;

    i = findopt(option, argc, argv);
    if (!i)
	return NULL;
    j = strlen(argv[i]) - 1;
    if ((argv[i][j] == option) && (i + 1 < argc)
	&& (argv[i + 1][0] != '-')) {
	return argv[i + 1];
    }
    return NULL;
}

/*
 * parse options global to all leafnode programs
 * conffile will be malloced if non-NULL
 */
int
parseopt(const char *progname, int option,
	 /*@null@*/ const char *opta, /*@null@*/ char **conffile)
{
    switch (option) {
    case 'V':
	printf("%s %s\n", progname, version);
	exit(0);
    case 'v':
	verbose++;
	return TRUE;
    case 'D':
	if (opta && *opta)
	    debugmode = atoi(opta);
	else
	    debugmode = ~0;
	return TRUE;
    case 'F':
	if (opta != NULL && *opta) {
	    if (conffile)
		*conffile = critstrdup(opta, "parseopt");
	    return TRUE;
	}
    }
    return FALSE;
}

static int compare(const void *a, const void *b,
		   const void *config __attribute__ ((unused)));
static int
compare(const void *a, const void *b,
	/*@unused@*/ const void *config __attribute__ ((unused)))
{
    return strcasecmp((const char *)a, (const char *)b);
}

static /*@null@*/ /*@only@*/ struct rbtree *rb_dormant;

/** Read directory with a list of groups into rbtree. Trouble is logged.
 *  \return rbtree, NULL in case of trouble.
 */
/*@null@*/ /*@only@*/ struct rbtree *
initgrouplistdir(const char *dir)
{
    DIR *d;
    struct dirent *de;
    mastr *t;
    static const char myname[] = "initgrouplistdir";
    struct rbtree *rb;

    t = mastr_new(LN_PATH_MAX);
    rb = rbinit(compare, 0);
    if (!rb) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot init red-black-tree, out of memory.");
	mastr_delete(t);
	return NULL;
    }

    (void)mastr_vcat(t, spooldir, dir, NULL);
    d = opendir(mastr_str(t));
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Unable to open directory %s: %m",
	       mastr_str(t));
	mastr_delete(t);
	rbdestroy(rb);
	return NULL;
    }
    while ((de = readdir(d)) != NULL) {
	char *k;
	const char *k2;

	if (de->d_name[0] == '.')
	    continue;
	k = critstrdup(de->d_name, myname);
	k2 = rbsearch(k, rb);
	if (NULL == k2) {
	    /* OOM */
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "out of memory in "
		   "%s, cannot build rbtree", myname);
	    exit(EXIT_FAILURE);
	}
	if (k != k2) {
	    /* key was already present in tree */
	    ln_log(LNLOG_SCRIT, LNLOG_CTOP,
		   "directory lists same file \"%s\" more than once!? "
		   "Confused, aborting.", k);
	    abort();
	}
	/* NOTE: rbsearch does NOT make a copy of k, so you must not
         * free k here! */
    }
    (void)closedir(d);
    mastr_delete(t);
    return rb;
}

/** Read lines of a file into rbtree, ignore empty lines.
 *  \return rbtree, NULL in case of trouble.
 */
/*@null@*/ /*@only@*/ struct rbtree *
initfilelist(FILE *f, const void *config,
	int (*comparison_function)(const void *, const void *, const void *))
{
    static const char myname[] = "initfilelist";
    struct rbtree *rb;
    char *l;

    rb = rbinit(comparison_function, config);
    if (!rb) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot init red-black-tree, out of memory.");
	return NULL;
    }

    while ((l = getaline(f)) && *l) {
	char *k1;
	const char *k2;

	k1 = critstrdup(l, myname);
	k2 = rbsearch(k1, rb);

	if (k2 == NULL) {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "out of memory in "
		   "%s, cannot build rbtree", myname);
	    free(k1);
	    freegrouplist(rb);
	    return NULL;
	}

	if (k2 != k1)	/* dupe */
	    free(k1);
    }
    return rb;
}

void
freegrouplist(/*@only@*/ struct rbtree *rb)
{
    RBLIST *r = rbopenlist(rb);
    const void *x;
    while ((x = rbreadlist(r))) {
	/* this is ugly, but rbreadlist delivers const */
	free((void *)x);
    }
    rbcloselist(r);
    rbdestroy(rb);
}

/************************
 * the same for dormant.groups
 */

/** Read dormant.groups directory. Trouble is logged.
 *  \return TRUE for success, FALSE in case of trouble.
 */
int
init_dormant(void)
{
    if (rb_dormant == NULL)
	rb_dormant = initgrouplistdir("/dormant.groups");
    if (rb_dormant)
	return TRUE;
    else
	return FALSE;
}

RBLIST *
open_dormant(void)
{
    return rbopenlist(rb_dormant);
}

const char *
read_dormant(RBLIST * r)
{
    return (const char *)rbreadlist(r);
}

void
close_dormant(RBLIST * r)
{
    rbcloselist(r);
}

void
free_dormant(void)
{
    if (rb_dormant) {
	freegrouplist(rb_dormant);
	rb_dormant = NULL;
    }
}

/**
 * check whether "groupname" is represented in 
 *   spool/dormant.groups 
 * touching the file. 
 */
int
is_dormant(const char *groupname)
{
    if (!init_dormant())
	exit(EXIT_FAILURE);
    return  rbfind(groupname, rb_dormant) != 0;
}

/* no good but this server isn't going to be scalable so shut up */
/*@dependent@*/ char *
lookup(/*@null@*/ const char *msgid)
{
    static /*@null@*/ /*@owned@*/ char *name = NULL;
    static unsigned int namelen = 0;
    unsigned int r;
    unsigned int i;
    char *p;
    const char *const myname = "lookup";

    if (!msgid || !*msgid)
	return NULL;

    i = strlen(msgid) + strlen(spooldir) + 30;

    if (!name) {
	name = (char *)critmalloc(i, myname);
	namelen = i;
    } else if (i > namelen) {
	name = (char *)critrealloc(name, i, myname);
	namelen = i;
    }

    p = mastrcpy(name, spooldir);
    p = mastrcpy(p, "/message.id/");
    (void)mastrcpy(p + 4, msgid);
    msgid_sanitize(p + 4);
    r = msgid_hash(p + 4);
    str_ulong0(p, r, 3);
    p[3] = '/';
    return name;
}

/** check if we already have an article with this message.id 
 \return 
   - 0 if article not present or mid parameter NULL 
   - 1 if article is already there */
/*@falsewhennull@*/ int
ihave(/*@null@*/ const char *mid
/** Message-ID of article to check, may be NULL */ )
{
    const char *m = lookup(mid);
    struct stat st;
    if (m && (0 == stat(m, &st))) {
	if (!S_ISREG(st.st_mode)) {
	    ln_log(LNLOG_SWARNING, LNLOG_CARTICLE,
		    "ihave(): article file %s is not a regular file (mode 0%o)",
		   m,  st.st_mode);
	    return 0;
	} else {
	    return 1;
	}
    }
    return 0;
}

/** make directory and all parents with mode 0775
 * \bug exits the program with EXIT_FAILURE if it cannot create or change
 * into directory.
 * \bug changes the current working directory
 * \return 1 for success, 0 if no directory name given */
static int
makedir(char *d)
{
    char *p;
    char *q;

    if (!d || *d != '/' || chdir("/"))
	return 0;
    q = d;
    do {
	*q = '/';
	p = q;
	q = strchr(++p, '/');
	if (q)
	    *q = '\0';
	if (!chdir(p))
	    continue;
	if (errno == ENOENT)
	    if (mkdir(p, (mode_t) MKDIR_MODE)) {
		ln_log(LNLOG_SERR, LNLOG_CGROUP, "mkdir %s: %m", d);
		exit(EXIT_FAILURE);
	    }
	if (chdir(p)) {
	    ln_log(LNLOG_SERR, LNLOG_CGROUP, "chdir %s: %m", d);
	    exit(EXIT_FAILURE);
	}
    } while (q);
    return 1;
}

/** convert newsgroup argument to a directory name and chdir there.
 * \return 1 for success, 0 for error
 */
int
chdirgroup(const char *group,
	   int creatdir /** if true, create missing directories */ )
{
    char *p;
    mastr *s = mastr_new(LN_PATH_MAX);
    size_t len;

    if (group && *group) {
	mastr_vcat(s, spooldir, "/", NULL);
	len = mastr_len(s);
	mastr_cat(s, group);
	p = mastr_modifyable_str(s) + len;
	while (*p) {
	    if (*p == '.')
		*p = '/';
	    else
		*p = tolower((unsigned char)*p);
	    p++;
	}
	if (!chdir(mastr_str(s))) {
	    mastr_delete(s);
	    return 1;		/* chdir successful */
	}
	if (creatdir) {
	    int err = makedir(mastr_modifyable_str(s));
	    mastr_delete(s);
	    return err;
	}
	mastr_modifyable_str(s)[len] = '\0';
	if (chdir(mastr_str(s))) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chdir(%s): %m", mastr_str(s));
	}
    }
    mastr_delete(s);
    return 0;
}

/* get the fully qualified domain name of this box into fqdn */
static void
whoami(void)
{
    struct hostent *he;
    int debugqual = 0;
    char *x;

    if ((x = getenv("LN_DEBUG_QUALIFICATION")) != NULL
	&& *x)
	debugqual = 1;

    if (!gethostname(fqdn, 255) && (he = gethostbyname(fqdn)) != NULL) {
	fqdn[0] = '\0';
	strncat(fqdn, he->h_name, 255);
	if (debugqual) ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
			      "canonical hostname: %s", fqdn);
	if (!is_validfqdn(fqdn)) {
	    char **alias;
	    alias = he->h_aliases;
	    while (alias && *alias) {
		if (debugqual) {
		    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
			   "alias for my hostname: %s", *alias);
		}
		if (is_validfqdn(*alias)) {
		    fqdn[0] = '\0';
		    strncat(fqdn, *alias, 255);
		    break;
		} else {
		    alias++;
		}
	    }
	}
    }
}

/*
 * Functions to handle stringlists
 */
/*
 * prepend string "newentry" to stringlist "list".
 */
void
prependtolist(struct stringlist **list, /*@unique@*/ const char *newentry)
{

    struct stringlist *ptr;

    ptr = (struct stringlist *)critmalloc(sizeof(struct stringlist) +
					  strlen(newentry),
					  "Allocating space in stringlist");
    strcpy(ptr->string, newentry);	/* RATS: ignore */
    ptr->next = *list;
    *list = ptr;
}

/*
 * append string "newentry" to stringlist "list". "lastentry" is a
 * pointer pointing to the last entry in "list" and must be properly
 * intialized.
 */
void
appendtolist(struct stringlist **list, struct stringlist **lastentry,
	     /*@unique@*/ const char *newentry)
{
    struct stringlist *ptr;
    ptr = (struct stringlist *)critmalloc(sizeof(struct stringlist) +
					  strlen(newentry),
					  "Allocating space in stringlist");

    strcpy(ptr->string, newentry);
    ptr->next = NULL;
    if (*list == NULL)
	*list = ptr;
    else
	(*lastentry)->next = ptr;
    *lastentry = ptr;
}

/*
 * find a string in a stringlist
 * return pointer to string if found, NULL otherwise
 */
/*@null@*/ char *
findinlist(/*@null@*/ struct stringlist *haystack, /*@null@*/ const char *const needle)
{
    struct stringlist *a;
    size_t n;

    if (needle == NULL)
	return NULL;

    n = strlen(needle);
    a = haystack;
    while (a && a->string) {
	if (strncmp(needle, a->string, n) == 0)
	    return a->string;
	a = a->next;
    }
    return NULL;
}

/*
 * find a string in a stringlist
 * return pointer to string if found, NULL otherwise
 */
struct stringlist **
lfindinlist(struct stringlist **haystack, char *needle, int len)
{
    struct stringlist **a;

    a = haystack;
    while (a && *a && (*a)->string) {
	if (strncmp(needle, (*a)->string, len) == 0)
	    return a;
	a = &(*a)->next;
    }
    return NULL;
}

void replaceinlist(struct stringlist **haystack, char *needle, int len)
{
    struct stringlist **f = lfindinlist(haystack, needle, len);
    struct stringlist *n;
    if (!f) prependtolist(haystack, needle);
    else {
	n = (*f)->next;
	free(*f);
	*f = (struct stringlist *)critmalloc(sizeof(struct stringlist) +
			strlen(needle), "Allocating space in stringlist");
	strcpy((*f)->string, needle);
	(*f)->next = n;
    }
}

/*
 * remove item from list
 * give pointer which is to twist
 */
void
removefromlist(struct stringlist **f)
{
    struct stringlist *l = *f;
    *f = (*f)->next;
    free(l);
}


/*
 * free a list
 */
void
freelist(/*@null@*/ /*@only@*/ struct stringlist *list)
{
    struct stringlist *a = list, *b;
    if (!list)
	return;

    while (a) {
	b = a->next;
	free(a);
	a = b;
    }
}

/*
 * get the length of a list
 */
int
stringlistlen(/*@null@*/ const struct stringlist *list)
{
    int i;

    for (i = 0; list; list = list->next)
	i++;
    return i;
}

/*
 * convert a space separated string into a stringlist
 */
/*@null@*/ /*@only@*/ struct stringlist *
cmdlinetolist(const char *cmdline)
{
    char *c;
    char *o;
    struct stringlist *s = NULL, *l = NULL;

    if (!cmdline || !*cmdline)
	return NULL;
    o = (char *)critmalloc(strlen(cmdline) + 1,
			   "Allocating temporary string space");
    c = o;
    strcpy(c, cmdline);
    while (*c) {
	char *p;

	SKIPLWS(c);
	p = c;
	SKIPWORDNS(c);
	if (*c)
	    *c++ = '\0';
	if (*p)
	    /* avoid lists with only one empty entry */
	    appendtolist(&s, &l, p);
    }
    free(o);
    return s;
}

/*
 * match str against patterns in a stringlist
 * return matching item or NULL
 */
/*@null@*/ /*@dependent@*/ struct stringlist*
matchlist(struct stringlist *patterns, const char *str)
{
    struct stringlist *a = patterns;
    while (a) {
	if (0 == ngmatch((const char *)&a->string, str))
	    break;
	a = a->next;
    }
    return a;
}

/*@dependent@*/ const char *
rfctime(void)
{
    static char date[40];
    size_t sz;
    time_t now, off;
    struct tm *local;
    int hours, mins;

    /* get local and Greenwich times */
    now = time(0);
    off = gmtoff(now);
    local = localtime(&now);
    mins = off / 60;
    hours = mins / 60;
    mins %= 60;
    /* finally print the string */
    sz = strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S", local);
    /* Timezone offset %z is not portable (though C99), add manually */
    if (sz && sz + 7 <= sizeof date)
	snprintf(date+sz, sizeof date - sz, " %+03d%02d", hours, mins);
    else
	*date = '\0';
    return date;
}

/*
 * returns 0 if string matches pattern
 */
int
ngmatch(const char *pattern, const char *str)
{
    int r = wildmat(str, pattern);
#if 0
    if (debugmode & DEBUG_MISC) {
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "ngmatch(pattern = \"%s\", "
	       "str = \"%s\") == %d", pattern, str, r);
    }
#endif
    return !r;
}

/*
 * return 1 if this (maybe nondeterminate) group is
 * (or all groups are) in delaybody mode
 */
int
delaybody_group(/*@null@*/ const char *group)
{
    struct delaybody_entry *e;

    if (delaybody || !group)
	return delaybody;

    for (e = delaybody_base; e; e = e->next) {
	if (!ngmatch(e->group, group))
	    return 1;
    }
    return 0;
}
