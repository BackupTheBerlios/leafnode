/*
texpire -- expire old articles

Written by Arnt Gulbrandsen <agulbra@troll.no> and copyright 1995
Troll Tech AS, Postboks 6133 Etterstad, 0602 Oslo, Norway, fax +47
22646949.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
and Randolf Skerka <Randolf.Skerka@gmx.de>.
Copyright of the modifications 1997.
Modified by Kent Robotti <robotti@erols.com>. Copyright of the
modifications 1998.
Modified by Markus Enzenberger <enz@cip.physik.uni-muenchen.de>.
Copyright of the modifications 1998.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
Copyright of the modifications 1998, 1999.
Modified by Kazushi (Jam) Marukawa <jam@pobox.com>.
Copyright of the modifications 1998, 1999.
Modified by Joerg Dietrich <joerg@dietrich.net>.
Copyright of the modifications 1999.

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "format.h"

#ifdef SOCKS
#include <socks.h>
#endif

#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

/* FIXME: xoverstuff belongs to xover.h or something */
#define SUBJECT 1
#define FROM 2
#define DATE 3
#define MESSAGEID 4
#define REFERENCES 5
#define BYTES 6
#define LINES 7
#define XREF 8

#define THREADSAFETY 99 /* We could set this to 1 if everybody obeyed
			 * grandson of RFC 1036 */

time_t now;

int default_expire;

int debug = 0;

int use_atime = 1;	/* look for atime on articles to expire */

struct exp {
    char * xover;	/* putting this at the end of the struct leads
    			   to segfaults, indicating there is something wrong */
    int kill;
    unsigned long artno; /* also present in xover, but needed so often that
			    we it store seperately */
};

char *xoverextract(char *xover, unsigned int field);
void savethread(struct exp *articles, char *refs, unsigned long acount);

void free_expire(void) {
    struct expire_entry *a, *b;

    b = expire_base;
    while ((a = b)) {
        b = a->next;
        free(a);
    }
}    

/*
05/27/97 - T. Sweeney - Find a group in the expireinfo linked list and return
                        its expire time. Otherwise, return zero.
*/

static time_t lookup_expire(char* group) {
    struct expire_entry *a;
   
    a = expire_base;
    while (a) {
	if (!ngmatch(a->group, group))
	    return a->xtime;
        a = a->next;
    }
    return 0;
}

/*
 * return 1 if xover is a legal overview line, 0 else
 */
static int legalxoverline (char * xover, unsigned long artno) {
    char * p;
    char * q;

    if (!xover)
	return 0;

    /* anything that isn't tab, printable ascii, or latin-* ? then killit */

    p = xover;
    while (*p) {
	int c = (unsigned char)*p++;

	if ((c != '\t' && c < ' ') || (c > 126 && c < 160)) {
	    if (debugmode)
		ln_log(LNLOG_DEBUG,
			"%lu xover error: non-printable chars.", artno);
	    return 0;
	}
    }

    p = xover;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG, "%lu xover error: no Subject: header.", artno);
	return 0;
    }

    /* article number */

    while(p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode)
        	ln_log(LNLOG_DEBUG, "%lu xover error: article "
		       "number must consists of digits.", artno);
	    return 0;
	}
	p++;
    }

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG, "%lu xover error: no From: header.", artno);
	return 0;
    }

    /* subject: no limitations */

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
            ln_log(LNLOG_DEBUG, "%lu xover error: no Date: header.", artno);
	return 0;
    }

    /* from: no limitations */

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
            ln_log(LNLOG_DEBUG,
		    "%lu xover error: no Message-ID: header.", artno);
	return 0;
    }

    /* date: no limitations */

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG,
		    "%lu xover error: no References: or Bytes: header.",
		    artno);
	return 0;
    }

    /* message-id: <*@*> */

    if (*p != '<') {
	if (debugmode)
	    ln_log(LNLOG_DEBUG,
		    "%lu xover error: Message-ID does not start with <.",
		    artno);
	return 0;
    }
    while (p != q && *p != '@' && *p != '>' && *p != ' ') {
	p++;
    }
    if (*p != '@') {
	if (debugmode)
	    ln_log(LNLOG_DEBUG,
		    "%lu xover error: Message-ID does not contain @.", artno);
	return 0;
    }
    while (p != q && *p != '>' && *p != ' ')
	p++;
    if ((*p != '>') 
	|| (++p != q)) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG,
		    "%lu xover error: Message-ID does not end with >.", artno);
	return 0;
    }

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG, "%lu xover error: no Bytes: header.", artno);
	return 0;
    }

    /* references: a series of <*@*> separated by space */

    while (p != q) {
	if (*p != '<') {
	    if (debugmode)
        	ln_log(LNLOG_DEBUG, "%lu xover error: "
			"Reference does not start with <.", artno);
	    return 0;
	}
	while (p != q && *p != '@' && *p != '>' && *p != ' ')
	    p++;
	if (*p != '@') {
	    if (debugmode)
        	ln_log(LNLOG_DEBUG,
			"%lu xover error: Reference does not contain @.",
			artno);
	    return 0;
	}
	while (p != q && *p != '>' && *p != ' ')
	    p++;
	if (*p++ != '>') {
	    if (debugmode)
        	ln_log(LNLOG_DEBUG,
			"%lu xover error: Reference does not end with >.",
			artno);
	    return 0;
	}
	while (p != q && *p == ' ')
	    p++;
    }

    p = q+1;
    q = strchr(p, '\t');
    if (!q) {
	if (debugmode)
	    ln_log(LNLOG_DEBUG, "%lu xover error: no Lines: header.", artno);
	return 0;
    }

    /* byte count */
    while(p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode)
		ln_log(LNLOG_DEBUG, "%lu xover error: illegal digit "
			"in Bytes: header.", artno);
	    return 0;
	}
	p++;
    }

    p = q+1;
    q = strchr(p, '\t');
    if (q)
        *q = '\0'; /* kill any extra fields */

    /* line count */
    while(p && *p && p != q) {
	if (!isdigit((unsigned char)*p)) {
	    if (debugmode)
        	ln_log(LNLOG_DEBUG, "%lu xover error: illegal digit "
			"in Lines: header.", artno);
	    return 0;
	}
	p++;
    }

    return 1;
}

/*
 * dogroup: expire group
 * 
 * works as follows:
 * (1) make an overview array of all articles in the current directory.
 * (2) mark all articles as to-be-deleted
 * (3) rescue those articles whose atime/mtime (dependent on whether
 *     texpire was called with -f) is newer than the expiry time or
 *     which are in a thread with a rescued article
 */
static void dogroup(struct newsgroup* g) {
    char gdir[PATH_MAX];
    char artfile[PATH_MAX];
    char *p;
    char *refs;
    char *msgid;
    DIR *d;
    struct dirent * de;
    struct stat st;
    unsigned long first, last, art, acount, current;
    struct exp* articles;
    int fd;
    char * overview; /* xover: read then free */

    int deleted,kept;

    acount = current = 0;
    deleted = kept = 0;
    clearidtree();

    /* eliminate empty groups */
    if (!chdirgroup(g->name, FALSE))
	return;
    getcwd(gdir, PATH_MAX);

    /* find low-water and high-water marks */

    d = opendir(".");
    if (!d) {
	ln_log(LNLOG_ERR, "opendir in %s: %s", gdir, strerror(errno));
	return;
    }

    first = ULONG_MAX;
    last = 0;
    while ((de = readdir(d)) != 0) {
	if (!isdigit((unsigned char)de->d_name[0]) ||
	     stat(de->d_name, &st) || !S_ISREG(st.st_mode))
	    continue;
	art = strtoul(de->d_name, &p, 10);
	if (p && !*p) {
	    acount++;
	    if (art < first)
		first = art;
	    if (art > last)
		last = art;
	}
    }

    closedir(d);
    if (last < first)
	return;

    if (debugmode)
	ln_log(LNLOG_DEBUG,
		"%s: expire %lu, low water mark %lu, high water mark %lu",
		g->name, expire, first, last);

    if (expire <= 0)
	return;

    /* allocate and clear article array */

    articles = (struct exp*)critmalloc((acount + 1) * sizeof(struct exp),
				       "Reading articles to expire");
    for (art = 0; art < acount; art++) {
	articles[art].xover = NULL;
	articles[art].kill = 0;
	articles[art].artno = 0;
    }

    /* read in overview info, to be purged and written back */

    overview = NULL;

    /* if overview file does not exist, create it */
    if (stat(".overview", &st))
	getxover();

    if (stat(".overview", &st) == 0) {
	/* could use mmap() here but I don't think it would help */
	overview = critmalloc((size_t)st.st_size + 1, "Reading article overview info");
	if ((fd = open(".overview", O_RDONLY)) < 0 ||
	    (read(fd, overview, (size_t)st.st_size) < (ssize_t)st.st_size)) {
	    ln_log(LNLOG_ERR, "can't open/read %s/.overview: %s", gdir,
		   strerror(errno));
	    *overview = '\0';
	    if (fd > -1)
		close(fd);
	} else {
	    close(fd);
	    overview[st.st_size] = '\0'; /* 0-terminate string */
	}

	p = overview;
	while (p && *p) {
	    while (p && isspace((unsigned char)*p))
		p++;
	    art = strtoul(p, NULL, 10);
	    if (art >= first && art <= last &&
		!articles[current].xover) {
		articles[current].xover = p;
		articles[current].artno = art;
		current++;
	    }
	    p = strchr(p, '\n');
	    if (p) {
		*p = '\0';
		if (p[-1] == '\r')
		    p[-1] = '\0';
		p++;
	    }
	}
    }

    /* check the syntax of the .overview info, and delete all 
       illegal stuff */
    
    for(current = 0; current < acount; current++) {
	if (!legalxoverline(articles[current].xover, 
			     articles[current].artno)) 
	    articles[current].xover = NULL;	/* memory leak */
    }

    /* insert articles in tree, and clear 'kill' for new or read articles */

    for(current = acount; current > 0; current--) {
	articles[current-1].kill = 1;	/* by default, kill all articles */
	str_ulong(artfile, articles[current-1].artno);
	if (stat(artfile, &st) == 0 && (S_ISREG(st.st_mode)) &&
	    ((st.st_mtime > expire) ||
	     (use_atime && (st.st_atime > expire)))) {
	    articles[current-1].kill = 0;
	    refs = xoverextract(articles[current-1].xover, REFERENCES);
	    msgid = xoverextract(articles[current-1].xover, MESSAGEID);

	    if (refs) {
		savethread(articles, refs, acount);
		free(refs);
		refs = NULL;
	    }
	    if (msgid) {
		if (findmsgid(msgid)) { /* another file with same msgid? */
		    articles[current-1].kill = 1;
		} else {
		    insertmsgid(msgid, articles[current-1].artno);
		    if (st.st_nlink < 2) { /* repair fs damage */
			if (link(artfile, lookup(msgid))) {
			    if (errno == EEXIST)
			    /* exists, but points to another file */
				articles[current-1].kill = 1;
			    else
				ln_log(LNLOG_ERR, 
				       "relink of %s failed: %s (%s)",
				       msgid, 
				       lookup(msgid), strerror(errno));
			}
			else
			    ln_log(LNLOG_INFO, "relinked message %s", msgid);
		    }
		}
	    } else if (articles[current-1].xover) {
		/* data structure inconsistency: delete and be rid of it */
		articles[current-1].kill = 1;
	    } else {
		/* possibly read the xover line into memory? */
	    }
	    if (msgid) {
		free(msgid);
		msgid = NULL;
	    }
	}
    }
    
    /* compute new low-water mark */

    first = last;
    for(art = 0; art < acount; art++) {
	if(!articles[art].kill && articles[art].artno < first)
	    first = articles[art].artno;
    }
    g->first = first;

    /* remove old postings */

    for (art = 0; art < acount ; art++) {
	if (articles[art].kill) {
	    str_ulong(artfile, articles[art].artno);
	    if (!unlink(artfile)) {
		if (debugmode)
		    ln_log(LNLOG_DEBUG, "deleted article %s/%lu", 
			    gdir, articles[art].artno);
		deleted++;
	    } else if (errno != ENOENT && errno != EEXIST) {
		/* if file was deleted already or it was not a file */
		/* but a directory, skip error message */
		kept++;
		ln_log(LNLOG_ERR, "unlink %s/%lu: %s", 
			gdir, articles[art].artno, strerror(errno));
	    } else {
		/* deleted by someone else */
	    }
	} else {
	    kept++;
	}
    }
    free(articles);
    free(overview);
    
    if (last > g->last) /* try to correct insane newsgroup info */
	g->last = last;

    if (deleted || kept) {
	ln_log(LNLOG_INFO,
	       "%s: %d articles deleted, %d kept", g->name, deleted, kept);
    }

    if (!kept) {
	if (unlink(".overview") < 0)
	    ln_log(LNLOG_ERR, "unlink %s/.overview: %s", gdir, 
		   strerror(errno));
	if (!chdir("..") && (isinteresting(g->name) == 0)) {
	    /* delete directory and empty parent directories */
	    while (rmdir(gdir) == 0) {
		getcwd(gdir, PATH_MAX);
		chdir("..");
	    }
	}
    }
    /* Once we're done and there's something left we have to update the
     * .overview file. Otherwise unsubscribed groups will never be
     * deleted.
     */
    getxover();
}
    
char *xoverextract(char *xover, unsigned int field) {
    unsigned int n;
    char *line;
    char *p, *q, *tmp;

    if (!xover)
	return NULL;

    tmp = p = strdup(xover); 
    for (n = 0; n < field; n++)
	if (p && (p = strchr(p + 1, '\t')))
	    p++;
    q = p ? strchr(p, '\t') : NULL;
    if (p && q) {
	*q = '\0';
	line = strdup(p);
    } else
	line = NULL;
    free(tmp);
    return line;
}

/*
 * mark all articles as not-to-be deleted which are contained in
 * the first THREADSAFETY entries of the References line "refs"
 * of the article we decided to save in dogroup()
 * this is rather slow because all articles are searched, and in groups
 * with many short threads, this will be done very repetitively.
 */
void savethread(struct exp *articles, char *refs, unsigned long acount) {
    char *p, *q;
    unsigned long i;
    unsigned int n = 0;
  
    p = refs;
    while((q = strchr(p, ' ')) && n < THREADSAFETY) {
	if (p && q)
	    *q = '\0';
	for(i = 0; i < acount; i++) {
	    if (articles[i].xover && articles[i].kill) {
		if (strstr(articles[i].xover, p)) {
		    if (debugmode)
			ln_log(LNLOG_DEBUG, "rescued thread %lu", 
			       articles[i].artno);
		    articles[i].kill = 0;
		}
	    }
	}
	p = q;
	p++;
	n++;
    }
    return;
}

static void expiregroup(struct newsgroup* g) {
    struct newsgroup * ng;

    ng = g;
    while (ng && ng->name) {
	if (!(expire = lookup_expire(ng->name)))
	    expire = default_expire;
	dogroup(ng);
	ng++;
    }
}


static void expiremsgid(void)
{
    int n;
    DIR * d;
    struct dirent * de;
    struct stat st;
    int deleted, kept;

    deleted = kept = 0;

    for (n=0; n<1000; n++) {
	sprintf(s, "%s/message.id/%03d", spooldir, n);
	if (chdir(s)) {
	    if (errno == ENOENT)
		mkdir(s, 0755); /* FIXME: this does not belong here */
	    if (chdir(s)) {
		ln_log(LNLOG_ERR, "chdir %s: %s", s, strerror(errno));
		continue;
	    }
	}

	d = opendir(".");
	if (!d)
	    continue;
	while ((de = readdir(d)) != 0) {
	    if (stat(de->d_name, &st) == 0) {
		if (st.st_nlink < 2 && !unlink(de->d_name))
		    deleted++;
		else if (S_ISREG(st.st_mode))
		    kept++;
	    }
	}
	closedir(d);
    }

    if (kept || deleted) {
	ln_log(LNLOG_INFO, "%d articles deleted, %d kept", deleted, kept);
    }
}

static void usage(void) {
    fprintf(stderr,
	"Usage:\n"
	"texpire -V\n"
	"    print version on stderr and exit\n"
	"texpire [-Dfv] [-F configfile]\n"
	"    -D: switch on debugmode\n"
	"    -f: force expire irrespective of access time\n"
	"    -v: more verbose (may be repeated)\n"
	"    -F: use \"configfile\" instead of %s/config\n"
	"See also the leafnode homepage at http://www.leafnode.org/\n",
	libdir);
}

int main(int argc, char** argv) {
    int option, reply;
    char * conffile;

    conffile = critmalloc(strlen(libdir) + 10,
			   "Allocating space for configuration file name");
    sprintf(conffile, "%s/config", libdir);

    if (!initvars(argv[0]))
	exit(EXIT_FAILURE);

    ln_log_open("texpire");

    while ((option=getopt(argc, argv, "F:VDvf")) != -1) {
	if (parseopt("texpire", option, optarg, conffile)) {
	    ;
	} else if (option == 'f') {
	    use_atime = 0;
	} else {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }
    debug = debugmode;
    expire = 0;
    expire_base = NULL;
    if ((reply = readconfig(conffile)) != 0) {
	ln_log(LNLOG_ERR, "Reading configuration from %s failed (%s).\n",
	       conffile, strerror(reply));
	unlink(lockfile);
	exit(2);
    }

    checkinteresting();

    if (lockfile_exists(FALSE, FALSE))
	exit(EXIT_FAILURE);
    readactive();
    if (!active) {
	/* FIXME remove this fprintf? */
	fprintf(stderr, "Reading active file failed, exiting "
			 "(see syslog for more information).\n"
			 "Has fetchnews been run?\n");
	unlink(lockfile);
	exit(2);
    }

    ln_log(LNLOG_INFO, "%s", 
	   use_atime ? "checking atime and mtime" : "checking mtime");

    if (expire == 0) {
	ln_log(LNLOG_ERR, "no expire time");
	exit(2);
    }

    now = time(NULL);
    default_expire = expire;

    expiregroup(active);
    writeactive();
    unlink(lockfile);
    expiremsgid();
    free_expire();
    return 0;
}
