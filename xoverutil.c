/*
libutil -- handling xover records

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

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>
#include <dirent.h>

#ifndef UIO_MAXIOV
#define UIO_MAXIOV 16
#endif

extern struct state _res;

/* global variables are initialized here */
unsigned long xfirst = 0;
unsigned long xlast = 0;
unsigned long xcount = 0;
struct xoverinfo * xoverinfo = NULL;

/* declarations */
char * getxoverline(const char * filename);
void writexover(void);

char * getxoverline(const char * filename) {
    char * l;
    char * result;
    FILE * f;
    struct stat st;
    struct utimbuf buf;

    /* We have to preserve atime and mtime to correctly
       expire unsubscribed groups */
    if (stat( filename, &st) != 0 )
	return NULL;
    buf.actime = st.st_atime;
    buf.modtime = st.st_mtime;
    
    result = NULL;
    debug = 0;
    if ((f=fopen( filename, "r")) ) {
	char * from;
	char * subject;
	char * date;
	char * msgid;
	char * references;
	int bytes;
	int linecount;
	char * xref;
	char ** h;
	int body;
	int i;

	from = subject = date = msgid = references = xref = NULL;
	bytes = linecount = 0;
	h = NULL;
	body = 0;

	while (!feof( f) && ( ( l = getaline( f )) != NULL ) ) {
	    if (l) {
		linecount++;
		bytes += strlen(l) + 2;
	    }
	    if (body || !l) {
		/* nothing */
	    } else if (!body && !*l) {
		linecount = 0;
		body = 1;
	    } else if (h && isspace((unsigned char)*l)) {
		if (*l) {
		    /* extend multiline headers */
		    (*h) = critrealloc(*h, strlen( *h) + strlen( l ) + 1,
					"extending header");
		    /* replace tabs with spaces */
		    for (i = strlen(l)-1; i >= 0; i--) {
			if (l[i] == '\t')
			    l[i] = ' ' ;
		    }
		    strcat(*h, l);
		}
	    } else if (!from && !strncasecmp( "From:", l, 5) ) {
		l += 5;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    from = strdup(l);
		    h = &from;
		}
	    } else if (!subject && !strncasecmp( "Subject:", l, 8) ) {
		l += 8;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    subject = strdup(l);
		    h = &subject;
		}
	    } else if (!date && !strncasecmp( "Date:", l, 5) ) {
		l += 5;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    date = strdup(l);
		    h = &date;
		}
	    } else if (!msgid && !strncasecmp( "Message-ID:", l, 11) ) {
		l += 11;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    msgid = strdup(l);
		    h = &msgid;
		}
	    } else if (!references &&
			!strncasecmp("References:", l, 11) ) {
		l += 11;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    references = strdup(l);
		    h = &references;
		}
	    } else if (!xref && !strncasecmp( "Xref:", l, 5) ) {
		l += 5;
		while (*l && isspace((unsigned char)*l))
		    l++;
		if (*l) {
		    xref = strdup(l);
		    h = &xref;
		}
	    } else {
		h = NULL;
	    }
	}
	if (from && date && subject && msgid && bytes) {
	    result = critmalloc(strlen( from) + strlen( date ) +
				 strlen(subject) + strlen( msgid ) +
				 (references ? strlen(references) : 0 ) +
				 100 + (xref ? strlen( xref) : 0),
				 "computing overview line");
	    sprintf(result, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d",
		     filename, subject, from, date, msgid,
		     references ? references : "" ,
		     bytes, linecount);
	    if (xref) {
		strcat(result, "\tXref: ");
		strcat(result, xref);
	    }
	}
	fclose(f);
	free (from);
	free (date);
	free (subject);
	free (msgid);
	free (references);
	free (xref);
    }
    debug = debugmode;
    utime(filename, &buf);
    return result;
}

/*
 * find xover line in sorted array, recursively
 */
static long helpfindxover(unsigned long article,
    unsigned long low, unsigned long high) {
    unsigned long mid;

    if (low > high)
	return -1;
    if (article == xoverinfo[low].artno)
	return low;
    if (article == xoverinfo[high].artno)
	return high;
    mid = (high-low)/2+low;
    if (article == xoverinfo[mid].artno)
	return mid;
    if (article < xoverinfo[mid].artno) {
	if (mid == 0)
	    return -1;
	else
	    return helpfindxover(article, low, mid-1);
    }
    else
	return helpfindxover(article, mid+1, high);
}

/*
 * return xover record of "article". -1 means failure.
 */
long findxover(unsigned long article) {
    return(helpfindxover( article, 0, xcount-1) );
}

/*
 * sort xoverfile 
 */
static int _compxover(const void *a, const void *b) {
    struct xoverinfo *la = (struct xoverinfo *)a;
    struct xoverinfo *lb = (struct xoverinfo *)b;
    if (la->artno < lb->artno)
	return -1;
    if (la->artno > lb->artno)
	return 1;
    return 0;
}

/*
 * read xoverfile into struct xoverinfo
 */
int getxover(void) {
    DIR * d;
    struct dirent *de;
    struct stat st;
    static char * overview = NULL;
    char *p, *q;
    int fd, update = 0;
    unsigned long current, art;
    long i;

    d = opendir(".");
    if (!d) {
	ln_log(LNLOG_ERR, "opendir: %s", strerror(errno));
	return 0;
    }

    /* read .overview file into memory */
    if (
	((fd=open(".overview", O_RDONLY)) >= 0)
	&& (fstat(fd, &st) == 0) 
	&& (overview=(char *)realloc(overview, (size_t)st.st_size + 1)) 
	&& (read(fd, overview, (size_t)st.st_size) == (ssize_t)st.st_size )
	) {
	close(fd);
	overview[st.st_size] = '\0';
    } else {
	/* .overview file not present: make a new one */
	if (fd >= 0) {
	    close(fd);
	}
	free(overview);
	overview = NULL;
    }

    /* find article range, without locking problems */
    xcount = 0;
    xlast = 0;
    xfirst = INT_MAX;
    while ((de=readdir(d))) {
	art = strtoul(de->d_name, &p, 10);
	if (art && p && !*p) {
	    xcount++;
	    if (art < xfirst)
		xfirst = art;
	    if (art > xlast)
		xlast = art;
	}
    }
    closedir(d);
    if (!xcount || xlast < xfirst)
	return 0;

    /* check number of entries in .overview file */
    p = overview;
    current = 0;
    while (p && *p) {
	q = strchr(p, '\n');
	if (q) {
	    p = q+1;
	    current++;
	}
	else
	    p = NULL;
    }

    if (xcount != current)
	update = 1;

    /* parse .overview file */
    xoverinfo = (struct xoverinfo *)critrealloc(
		  (char*)xoverinfo, sizeof(struct xoverinfo)*(xcount+current+1),
		  "allocating overview array");
    memset(xoverinfo, 0, sizeof(struct xoverinfo) * (xcount+current+1));

    p = overview;
    current = 0;
    while (p && *p) {
	while (p && isspace((unsigned char)*p))
	    p++;
	q = strchr(p, '\n');
	if (q)
	    *q++ = '\0';
	art = strtoul(p, NULL, 10);
	if (art > xlast || art < xfirst)
	    update = 1;
	else if (xoverinfo[current].text) {
	    xoverinfo[current].text = p;	/* leak memory */
	    xoverinfo[current].exists = 0;
	    xoverinfo[current].artno = art;
	    current++;
	}
	else {
	    xoverinfo[current].exists = 0;
	    xoverinfo[current].text = p;
	    xoverinfo[current].artno = art;
	    current++;
	}
	p = q;
    }

    /* compare .overview contents with files on disk */
    d = opendir(".");
    if (!d) {
	ln_log(LNLOG_ERR, "opendir %s: %s", getcwd(s, 1024),
	       strerror(errno));
	return 0;
    }
    while ((de = readdir(d)) != NULL) {
	art = strtoul(de->d_name, NULL, 10);
	if (art >= xfirst && art <= xlast) {
	    if (overview) {
		i = findxover(art);
		if (i >= 0) {
		    xoverinfo[i].exists = 1;
		    continue;
		}
	    }
	    if (stat( de->d_name, &st)) {
		ln_log(LNLOG_DEBUG, "Can't stat %s", de->d_name);
		continue;
	    }
	    else if (S_ISREG( st.st_mode) ) {
		xoverinfo[current].text = getxoverline(de->d_name);
	    }
	    else
		/* probably a directory */
		continue;
	    if (xoverinfo[current].text != NULL) {
		xoverinfo[current].exists = 1;
		xoverinfo[current].artno = art;
		update = 1;
		current++;
	    }
	    else {
		/* error getting xoverline from article - delete it */
		getcwd(s, 1024);
		ln_log(LNLOG_NOTICE, "illegal article: %s/%s", s, de->d_name);
		if ((lstat( de->d_name, &st) == 0) && S_ISREG(st.st_mode) ) {
		    if (unlink( de->d_name) )
			ln_log(LNLOG_WARNING, "failed to remove %s/%s",
				s, de->d_name);
		}
		else
		    ln_log(LNLOG_WARNING, "%s/%s is not a regular file",
			    s, de->d_name);
	    }
	} /* if (art >= xfirst && art <= xlast) */
    } /* while */
    closedir(d);

    /* free superfluous memory */
    xoverinfo = (struct xoverinfo *)critrealloc((char*)xoverinfo,
	sizeof(struct xoverinfo)*(current+2),
	"allocating overview array");

    qsort(xoverinfo, current, sizeof( struct xoverinfo), &_compxover);
    xcount = current;

    if (update)
	writexover();
    return 1;
}

/*
 * write .overview file
 */
void writexover(void) {
    char newfile[20];
    char lineend[3];
    struct iovec oooh[UIO_MAXIOV];
    int wfd, vi, vc, va;
    unsigned long art;

    strcpy(newfile, ".overview.XXXXXX");
    strcpy(lineend, "\n");
#ifdef HAVE_MKSTEMP
    if ((wfd=mkstemp(newfile)) == -1) {
	ln_log(LNLOG_ERR, "mkstemp of new .overview failed: %s",
	       strerror(errno));
	return;
    }
#else
    if (!( wfd=open( mktemp(newfile), O_WRONLY|O_CREAT|O_EXCL, 0664) ) ) {
	ln_log(LNLOG_ERR,
		"open(O_WRONLY|O_CREAT|O_EXCL) of new .overview failed: %s",
	       strerror(errno));
	return;
    }
#endif

    vi = vc = va = 0;
    for (art=0; art < xcount; art++) {
	if (xoverinfo[art].exists && xoverinfo[art].text) {
	    oooh[vi].iov_base = xoverinfo[art].text;
	    oooh[vi].iov_len = strlen(xoverinfo[art].text);
	    vc += oooh[vi++].iov_len + 1;
	    oooh[vi].iov_base = lineend;
	    oooh[vi++].iov_len = 1;
	    if (vi >= (UIO_MAXIOV - 1)) {
		if (writev( wfd, oooh, vi) != vc ) {
		    ln_log(LNLOG_ERR, "writev() for .overview failed: %s",
			   strerror(errno));
		    art = xlast+1;	/* so the loop will stop */
		}
		vi = vc = 0;
		va = 1;
	    }
	}
    }
    if (vi) {
	if (writev( wfd, oooh, vi) != vc )
	    ln_log(LNLOG_ERR, "writev() for .overview failed: %s",
		   strerror(errno));
	else
	    va = 1;
    }
    fchmod(wfd, (mode_t)0664);
    close(wfd);
    if (va) {
	if (rename( newfile, ".overview") ) {
	    if (unlink( newfile) )
		ln_log(LNLOG_ERR, "rename() and unlink() both failed: %s",
		       strerror(errno));
	    else
		ln_log(LNLOG_ERR, "rename(%s/%s, .overview) failed: %s",
		       getcwd(s, 1024), newfile,
		       strerror(errno));
	}
	else {
	    if (debugmode)
		ln_log(LNLOG_DEBUG, "wrote %s/.overview", getcwd(s, 1024));
	}
    }
    else {
	unlink(newfile);
	/* the group must be newly empty: I want to keep the old
	   .overview file I think */
    }
}

/*
 * Fix overview lines for every group in g. g is a comma-delimited string
 * which may contain spaces.
 */
void gfixxover(char * g) {
    char * q;

    if (!g)
        return;
    while (g && *g) {
        while (isspace((unsigned char)*g) )
            g++;
        q = strchr(g, ',');
        if (q)
            *q++ = '\0';
        if (chdirgroup( g, FALSE) && findgroup( g ) )
            getxover();
        g = q;
    }
}

void fixxover(void) {
    DIR * d;
    struct dirent * de;

    sprintf(s, "%s/interesting.groups", spooldir);
    d = opendir(s);
    if (!d) {
	ln_log(LNLOG_ERR, "opendir %s: %s", s,
	       strerror(errno));
	return;
    }

    while ((de = readdir(d)))  {
	if (( de->d_name[0] != '.') && findgroup( de->d_name ) ) {
	    if (chdirgroup( de->d_name, FALSE) ) {
		getxover();
	    }
	}
    }

    closedir(d);
}

void freexover(void) {
    if(xoverinfo) {
	free(xoverinfo);
	xoverinfo = 0;
    }
}
