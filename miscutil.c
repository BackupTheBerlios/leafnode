/** \file miscutil
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
    {"backup.moderated", 0755},	/* carries articles to moderated upstream groups */
    {"interesting.groups", 0755},
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
initvars(const char *const progname)
{
    uid_t ui;
    gid_t gi;
    struct mydir const *md = &dirs[0];

    /*@-noeffect@*/
    (void)progname;		/* shut up compiler warnings */
    /*@=noeffect@*/

#ifndef TESTMODE
    if (uid_getbyuname("news", &ui)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot uid_getbyuname(news,&ui): %m\n");
	return FALSE;
    }
    if (gid_getbyuname("news", &gi)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot gid_getbyuname(news,&gi): %m\n");
	return FALSE;
    }
#endif /* not TESTMODE */

    /* config.c stuff does not have to be initialized */
    expire_base = NULL;
    /* These directories should exist anyway */
    while (md->name) {
	char x[PATH_MAX];

	snprintf(x, sizeof(x), "%s/%s", spooldir, md->name);
	if (mkdir(x, (mode_t) 0110) && errno != EEXIST) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot mkdir(%s): %m\n", x);
	    return FALSE;
	}
#ifndef TESTMODE
	if (chown(x, ui, gi)) {	/* Flawfinder: ignore */
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chown(%s,%ld,%ld): %m\n",
		   x, (long)ui, (long)gi);
	    return FALSE;
	}
#endif /* not TESTMODE */
	if (log_chmod(x, md->m)) {
	    return FALSE;
	}
	md++;
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
 */
int
parseopt(const char *progname, int option,
	 /*@null@*/ const char *opta, char *conffile, size_t conffilesize)
{
    if (option == 'V') {
	printf("%s %s\n", progname, version);
	exit(0);
    } else if (option == 'v') {
	verbose++;
	return TRUE;
    } else if (option == 'D') {
	if (opta && *opta)
	    debugmode = atoi(opta);
	else
	    debugmode = ~0;
	return TRUE;
    } else if ((option == 'F') && opta != NULL && *opta) {
	return mastrncpy(conffile, opta, conffilesize) ? 1 : 0;
    }
    return FALSE;
}

/**
 * Check when the last posting has arrived in the group. \return 0 in
 * case of trouble, st_ctime of LASTPOSTED file of that group otherwise.  
 */
static time_t
getlastart(const char *group) /*@globals errno, spooldir;@*/ ;
static time_t
getlastart(const char *group)
  /*@globals errno, spooldir;@*/
{
    mastr *g;
    char *p = critstrdup(group, "getlastart"), *q;
    struct stat st;
    time_t ret = 0;

    g = mastr_new(PATH_MAX);
    for (q = p; *q; q++)
	if (*q == '.')
	    *q = '/';
	else
	    *q = tolower((unsigned char)*q);
    mastr_vcat(g, spooldir, "/", p, "/", LASTPOSTING, 0);
    free(p);
    if (stat(mastr_str(g), &st)) {
	if (errno != ENOENT) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot stat %s: %m", mastr_str(g));
	}
    } else {
	ret = st.st_ctime;
    }
    mastr_delete(g);
    return ret;
}

/**
 * Check which groups are still interesting and unsubscribe groups
 * which are not.
 */
void
checkinteresting(void)
{
    time_t now;
    struct dirent *de;
    struct stat st;
    DIR *d;

    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP, "expiring interesting.groups");

    now = time(NULL);
    if (chdir(spooldir) || chdir("interesting.groups")) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unable to chdir to %s/interesting.groups: %m", spooldir);
	return;
    }
    d = opendir(".");
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unable to opendir %s/interesting.groups: %m", spooldir);
	return;
    }
    while ((de = readdir(d)) != NULL) {
	time_t lastart = getlastart(de->d_name);
	if ((stat(de->d_name, &st) == 0) && S_ISREG(st.st_mode)) {
/*@dependent@*//*@null@*/ const char *reason;
	    /* reading a newsgroup changes the ctime (in
	     * markinterest()), if the newsgroup is newly created, the
	     * mtime is changed as well */
	    /* timeout_long for read groups */
	    if ((lastart - st.st_ctime > (timeout_long * SECONDS_PER_DAY))) {
		reason =
		    "established group not read for \"timeout_long\" days after last article";
	    }
	    /* timeout_short for unread groups */
	    else if ((st.st_mtime == st.st_ctime) /* unread so far */ &&
		     (lastart - st.st_ctime >
		      (timeout_short * SECONDS_PER_DAY))) {
		reason =
		    "newly-arrived group not read for \"timeout_short\" days after last article";
	    } else
		reason = NULL;
	    if (reason) {
		ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		       "skipping %s from now on: %s\n", de->d_name, reason);
		(void)log_unlink(de->d_name);
		/* don't reset group counts because if a group is
		   resubscribed, new articles will not be shown */
	    }
	}
    }
    (void)closedir(d);
}

static int compare(const void *a, const void *b,
		   const void *config __attribute__ ((unused)));
static int
compare(const void *a, const void *b,
	/*@unused@*/ const void *config __attribute__ ((unused)))
{
    return strcasecmp((const char *)a, (const char *)b);
}

static struct rbtree *rb;

/** Read interesting.groups directory. Trouble is logged.
 *  \return TRUE for success, FALSE in case of trouble.
 */
int
initinteresting(void)
{
    DIR *d;
    struct dirent *de;
    mastr *t;
    const char *const myname = "initinteresting";

    if (rb)
	return TRUE;

    t = mastr_new(PATH_MAX);
    rb = rbinit(compare, 0);
    if (!rb) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot init red-black-tree, out of memory.");
	mastr_delete(t);
	return 0;
    }

    (void)mastr_vcat(t, spooldir, "/interesting.groups", 0);
    d = opendir(mastr_str(t));
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Unable to open directory %s: %m",
	       mastr_str(t));
	mastr_delete(t);
	return FALSE;
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
	    free(k);
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
    return TRUE;
}

/** Read interesting.groups directory, terminate program in case of
    trouble. */
void
critinitinteresting(void)
{
    if (!initinteresting())
	exit(EXIT_FAILURE);
}

RBLIST *
openinteresting(void)
{
    return rbopenlist(rb);
}

const char *
readinteresting(RBLIST * r)
{
    return (const char *)rbreadlist(r);
}

void
closeinteresting(RBLIST * r)
{
    rbcloselist(r);
}

void
freeinteresting(void)
{
    if (rb) {
	RBLIST *r = openinteresting();
	const char *x;
	while ((x = readinteresting(r))) {
	    /* this is ugly, but readinteresting delivers const */
	    free((char *)x);
	}
	closeinteresting(r);
	rbdestroy(rb);
	rb = NULL;
    }
}

/**
 * check whether "groupname" is represented in interesting.groups, without
 * touching the file.
 */
int
is_interesting(const char *groupname)
{
    /* Local groups are always interesting. At least for the server :-) */
    if (is_localgroup(groupname))
	return TRUE;

    critinitinteresting();	/* does not return in case of trouble */

    return rbfind(groupname, rb) != 0;
}

/* no good but this server isn't going to be scalable so shut up */
char *
lookup(/*@null@*/ const char *msgid)
{
    static char *name = NULL;
    static unsigned int namelen = 0;
    unsigned int r;
    unsigned int i;
    char *p, *q;
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
    q = mastrcpy(p + 4, msgid);
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
int
ihave(const char *mid
/** Message-ID of article to check, may be NULL */ )
{
    const char *m = lookup(mid);
    struct stat st;
    if (m && (0 == stat(m, &st)) && S_ISREG(st.st_mode))
	return 1;
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

/** convert newsgroup argument to a directory name an chdir there.
 * \return: 1 for success, 0 for error
 */
int
chdirgroup(const char *group,
	   int creatdir /** if true, create missing directories */ )
{
    char *p;
    char s[PATH_MAX];		/* FIXME: possible overrun below */

    if (group && *group) {
	p = mastrcpy(s, spooldir);
	*p++ = '/';
	strcpy(p, group);
	while (*p) {
	    if (*p == '.')
		*p = '/';
	    else
		*p = tolower((unsigned char)*p);
	    p++;
	}
	if (!chdir(s))
	    return 1;		/* chdir successful */
	if (creatdir)
	    return makedir(s);
	s[strlen(spooldir)] = '\0';
	if (chdir(s)) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chdir(%s): %m", s);
	}
    }
    return 0;
}

/* get the fully qualified domain name of this box into fqdn */
void
whoami(void)
{
    struct hostent *he;

    if (!gethostname(fqdn, FQDN_SIZE - 1)
	&& (he = gethostbyname(fqdn)) != NULL) {
	strncpy(fqdn, he->h_name, FQDN_SIZE - 1);
	if (strchr(fqdn, '.') == NULL) {
	    char **alias;

	    alias = he->h_aliases;
	    while (alias && *alias)
		if (strchr(*alias, '.') && (strlen(*alias) > strlen(fqdn)))
		    strncpy(fqdn, *alias, FQDN_SIZE - 1);
		else
		    alias++;
	}
    } else {
	*fqdn = '\0';
    }

    endhostent();
}

/*
 * Functions to handle stringlists
 */
/*
 * append string "newentry" to stringlist "list". "lastentry" is a
 * pointer pointing to the last entry in "list" and must be properly
 * intialized.
 */
void
appendtolist(struct stringlist **list, struct stringlist **lastentry,
	     char *newentry)
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
findinlist(struct stringlist *haystack, /*@null@*/ const char *const needle)
{
    struct stringlist *a;

    if (needle == NULL)
	return NULL;
    a = haystack;
    while (a && a->string) {
	if (strncmp(needle, a->string, strlen(needle)) == 0)
	    return a->string;
	a = a->next;
    }
    return NULL;
}

/*
 * free a list
 */
void
freelist( /*@only@*/ struct stringlist *list)
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
stringlistlen(const struct stringlist *list)
{
    int i;

    for (i = 0; list; list = list->next)
	i++;
    return i;
}

/*
 * convert a space separated string into a stringlist
 */
struct stringlist *
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

	while (*c && isspace((unsigned char)*c))
	    c++;
	p = c;
	while (*c && !isspace((unsigned char)*c))
	    c++;
	if (*c)
	    *c++ = '\0';
	if (*p)
	    /* avoid lists with only one empty entry */
	    appendtolist(&s, &l, p);
    }
    free(o);
    return s;
}

/* next few routines implement a mapping from message-id to article
   number, and clearing the entire space */
struct msgidtree {
    struct msgidtree *left;
    struct msgidtree *right;
    int art;
    char msgid[1];
};

							/*@only@*//*@null@*/ static struct msgidtree *head;
							/* starts as NULL */
void
insertmsgid(const char *msgid, unsigned long art)
{
    struct msgidtree **a;
    int c;

    if (strchr(msgid, '@') == 0)
	return;
    a = &head;
    while (a) {
	if (*a) {
	    /* comparing only by msgid is uncool because the tree becomes
	       very unbalanced */
	    c = strcmp(strchr((*a)->msgid, '@'), strchr(msgid, '@'));
	    if (c == 0)
		c = strcmp((*a)->msgid, msgid);
	    if (c < 0)
		a = &((*a)->left);
	    else if (c > 0)
		a = &((*a)->right);
	    else {
		return;
	    }
	} else {
	    *a = (struct msgidtree *)
		critmalloc(sizeof(struct msgidtree) + strlen(msgid),
			   "Building expiry database");

	    (*a)->left = NULL;
	    (*a)->right = NULL;
	    strcpy((*a)->msgid, msgid);
	    (*a)->art = art;
	    return;
	}
    }
}

int
findmsgid(const char *msgid)
{
    struct msgidtree *a;
    int c;
    char *domainpart;

    /* domain part differs more than local-part, so try it first */
    domainpart = strchr(msgid, '@');
    if (domainpart == NULL)
	return 0;
    a = head;
    while (a) {
	c = strcmp(strchr(a->msgid, '@'), domainpart);
	if (c == 0)
	    c = strcmp(a->msgid, msgid);
	if (c < 0)
	    a = a->left;
	else if (c > 0)
	    a = a->right;
	else
	    return a->art;
    }
    return 0;
}

static void
begone( /*@only@*//*@null@*/ struct msgidtree *m)
{
    if (m) {
	begone(m->right);
	begone(m->left);
	free((char *)m);
    }
}

void
clearidtree(void)
{
    begone(head);
    head = NULL;
}

#ifndef HAVE_GMTOFF
extern time_t timezone;
#endif

typedef /*@observer@*/ const char *litstring;

/*@dependent@*/ const char *
rfctime(void)
{
    static char date[128];
    litstring months[] = { "Jan", "Feb", "Mar", "Apr",
	"May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    litstring days[] = { "Sun", "Mon", "Tue", "Wed",
	"Thu", "Fri", "Sat"
    };
    time_t now;
    struct tm gmt;
    struct tm local;
    int hours, mins;

    /* get local and Greenwich times */
    now = time(0);
    gmt = *(gmtime(&now));
    local = *(localtime(&now));
#ifdef HAVE_GMTOFF
    hours = local.tm_gmtoff / 60 / 60;
    mins = local.tm_gmtoff / 60 % 60;
#else
    /* this is empirical */
    hours = -timezone / 60 / 60 + ((local.tm_isdst > 0) ? 1 : 0);
    mins = -timezone / 60 % 60;
#endif
    /* finally print the string */
    sprintf(date, "%3s, %d %3s %4d %02d:%02d:%02d %+03d%02d",
	    days[local.tm_wday], local.tm_mday, months[local.tm_mon],
	    local.tm_year + 1900, local.tm_hour, local.tm_min,
	    local.tm_sec, hours, mins);
    return date;
}

/*
 * returns 0 if string matches pattern
 */
int
ngmatch(const char *pattern, const char *str)
{
    int r = wildmat(str, pattern);
    if (debug & DEBUG_MISC) {
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "ngmatch(pattern = \"%s\", "
	       "str = \"%s\") == %d", pattern, str, r);
    }
    return !r;
}
