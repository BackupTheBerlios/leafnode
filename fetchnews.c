/*
fetchnews -- post articles to and get news from upstream server(s)

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
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <utime.h>

int debug = 0;

unsigned long globalfetched = 0;
unsigned long globalkilled = 0;
unsigned long groupfetched;
unsigned long groupkilled;

struct serverlist * current_server;

int artno;

time_t now;

/* Variables set by command-line options which are specific for fetch */
unsigned long extraarticles = 0;
long windowsize = 5;    /* number of NNTP commands to pipeline */
unsigned int throttling = 0;	
                        /* the higher the value, the less bandwidth is used */
int postonly = 0;	/* if 1, don't read files from upstream server */
int noexpire = 0;	/* if 1, don't automatically unsubscribe newsgroups */
int forceactive = 0;	/* if 1, reread complete active file */
int headerbody = 0;	/* if 1, get headers only; if 2, get bodies only */

static jmp_buf jmpbuffer ;

int isgrouponserver( char * newsgroups );
int ismsgidonserver( char * msgid );
void delposted( time_t before );
unsigned long getarticle( struct filterlist *filter );
unsigned long getgroup ( struct newsgroup *g , unsigned long server );
int postarticles( void );

static void _ignore_answer( FILE * f ) {
    char * l=0;
    while ( ((l=getaline(f)) != NULL) && strcmp(l, ".") )
	;
}

static void sigcatch( int signo ) {
    if ( signo == SIGINT || signo == SIGTERM || signo == SIGALRM )
	longjmp( jmpbuffer, 1 );
    else if ( signo == SIGUSR1 )
	verbose++;
    else if ( signo == SIGUSR2 )
	verbose--;
}

static struct serverlist * findserver( char * servername ) {
    struct serverlist * sl;

    sl = servers;
    while ( sl ) {
	if ( sl->name && strcasecmp( servername, sl->name ) == 0 ) {
	    return sl;
	}
	sl = sl->next;
    }
    return NULL;
}

static int testheaderbody( char optchar ) {
    if ( !delaybody ) {
	printf( "Option -%c can only be used in conjunction with delaybody -"
		"ignored\n", optchar );
	return 0;
    }
    if ( headerbody == 1 ) {	/* already seen -H */
	printf( "Option -%c and -H cannot be used together - ignored\n",
		optchar );
	return 0;
    }
    if ( headerbody == 2 ) {	/* already seen -B */
	printf( "Option -%c and -B cannot be used together - ignored\n",
		optchar );
	return 0;
    }
    return 1;
}

static void usage( void ) {
    fprintf( stderr, "Usage:\n"
	"fetchnews -V\n"
	"    print version on stderr and exit\n"
	"fetchnews [-Dv] [-F configfile] [-S server] -M message-id\n"
	"fetchnews [-BDHnv] [-x #] [-F configfile] [-S server] [-t #] -N newsgroup\n"
	"fetchnews [-BDfHnPv] [-x #] [-F configfile] [-S server] [-t #]\n"
	"    -B: get article bodies only (works only with \"delaybody\" set)\n"
	"    -D: switch on debugmode\n"
	"    -f: force reload of groupinfo file\n"
	"    -F: use \"configfile\" instead of %s/config\n"
	"    -H: get article headers only (works only with \"delaybody\" set)\n"
	"    -n: switch off automatic unsubscription of groups\n"
	"    -P: post only, don't get new articles\n"
	"    -v: verbose mode (may be repeated)\n"
	"    -N newsgroup: get only articles in \"newsgroup\"\n"
	"    -S server: only get articles from \"server\"\n"
	"    -t delay: wait \"delay\" seconds between articles\n"
	"See also the leafnode homepage at\n"
	"    http://www.leafnode.org/\n",
	libdir
    );
}

/*
 * check whether there are any articles to post
 */
static int checkforpostings( void ) {
    DIR * d;
    struct dirent * de;
    int error;

    if ( chdir( spooldir ) || chdir ( "out.going" ) ) {
	error = errno;
        syslog( LOG_ERR, "Unable to cd to outgoing directory: %m" );
        printf( "Unable to cd to %s/out.going: %s\n", spooldir,
		strerror(error) );
        return FALSE;
    }

    d = opendir( "." );
    if ( !d ) {
	error = errno;
        syslog( LOG_ERR, "Unable to opendir out.going: %m" );
        printf( "Unable to opendir %s/out.going/: %s\n", spooldir,
		strerror(error) );
        return FALSE;
    }

    while ( (de=readdir( d )) != NULL ) {
	/* filenames of articles to post begin with digits */
	if ( isdigit( (unsigned char )de->d_name[0] ) )
	    return TRUE;
    }
    return FALSE;
}

/*
 * check whether any of the newsgroups is on server
 * return TRUE if yes, FALSE otherwise
 */
int isgrouponserver( char * newsgroups ) {
    char * p, *q;
    int retval;

    if ( !newsgroups )
	return FALSE;

    retval = FALSE;
    p = newsgroups;
    do {
	q = strchr( p, ',' );
	if ( q )
	    *q++ = '\0';
	putaline( "GROUP %s", p );
	if ( nntpreply() == 211 )
	    retval = TRUE;
	p = q;
	while ( p && *p && isspace((unsigned char)*p) )
	    p++;
    } while ( q && !retval );

    return retval;
}

/*
 * check whether message-id is on server
 * return TRUE if yes, FALSE otherwise
 *
 * Since the STAT implementation is buggy in some news servers (esp.
 * nntpcache), we use HEAD instead, although this causes more traffic.
 */
int ismsgidonserver( char * msgid ) {
    char *l;
    long a;

    if ( !msgid )
        return FALSE;
    putaline( "HEAD %s", msgid );
    l = getaline( nntpin );
    if ( get_long( l, &a ) == 1 && a == 221 ) {
	_ignore_answer( nntpin );
	return TRUE;
    }
    else {
	return FALSE;
    }
}

/*
 * delete posted articles. Everything in out.going/ that has a ctime smaller
 * than the time at which fetch was started is considered posted (if anything
 * had failed during posting, there is still a copy in failed.postings/ ).
 */
void delposted( time_t before ) {
    struct stat st;
    struct dirent * de;
    DIR * d;

    if ( chdir( spooldir ) || chdir ( "out.going" ) ) {
	printf( "Unable to cd to outgoing directory\n" );
        syslog( LOG_ERR, "Unable to cd to outgoing directory: %m" );
        return;
    }

    d = opendir( "." );
    if ( !d ) {
	syslog( LOG_ERR, "Unable to opendir out.going: %m" );
	printf( "Unable to opendir %s/out.going\n", spooldir );
	return;
    }

    while( ( de = readdir( d ) ) != NULL ) {
	if ( ( stat( de->d_name, &st ) == 0 ) && S_ISREG( st.st_mode ) &&
	     ( st.st_ctime < before ) ) {
	    if ( unlink( de->d_name ) )
		syslog( LOG_ERR, "Failed to unlink %s: %m", de->d_name );
	}
    }
    closedir( d );
}

/*
 * get an article by message id
 */
static int getbymsgid( char *msgid ) {
    putaline( "ARTICLE %s", msgid );
    return ( getarticle( NULL ) > 0 ) ? TRUE : FALSE;
}

/*
 * Get bodies of messages that have marked for download.
 * The group must already be selected at the remote server and
 * the current directory must be the one of the group.
 */
static void getmarked( struct newsgroup* group ) {
    FILE *f;
    char *l;
    struct stringlist * failed = NULL;
    struct stringlist * ptr = NULL;
    char filename[ 1024 ];

    if ( verbose )
        printf( "Getting bodies of marked messages for group %s ...\n",
                group->name );
    sprintf( filename, "%s/interesting.groups/%s", spooldir, group->name );
    if ( ! ( f = fopen( filename, "r" ) ) ) {
        syslog( LOG_ERR, "Cannot open %s for reading", filename );
	return;
    }

    while ( ( l = getaline( f ) ) ) {
	putaline( "ARTICLE %s", l );
	if ( !getarticle( NULL ) )
	    appendtolist( &failed, &ptr, l );
    }
    fclose( f );

    truncate( filename, (off_t)0 );	/* kill file contents */
    if ( !failed )
	return;

    /* write back ids of all articles which could not be retrieved */
    if ( ! ( f = fopen( filename, "w" ) ) )
	syslog( LOG_ERR, "Cannot open %s for writing", filename );
    else {
	ptr = failed;
	while ( ptr ) {
	    fprintf( f, "%s\n", ptr->string );
	    ptr = ptr->next ;
	}
	fclose( f );
    }
    freelist( failed );
}

/*
 * get newsgroup from a server. "server" is the last article that
 * was previously read from this group on that server
 */

/*
 * fopenmsgid: open a message id file, repairing errors if possible
 */
static FILE * fopenmsgid( const char * filename, int overwrite ) {
    FILE * f;
    char * c, *slp;
    struct stat st;

    if ( !stat( filename, &st ) ) {
	if ( !overwrite ) {
	    syslog( LOG_INFO, "article %s already stored", filename );
	    return NULL;
	}
	else {
	    f = fopen( filename, "w" );
	    if ( f == NULL )
		syslog( LOG_ERR, "unable to store article %s: %m", filename );
	    return f;
	}
    }
    else if ( errno == ENOENT ) {
	if ( (f = fopen( filename, "w" )) == NULL) {
	    /* check whether directory exists and possibly create new one */
	    c = strdup( filename );
	    slp  = strrchr( c, '/' );
	    if ( slp )
		*slp = '\0';
	    if ( stat( c, &st ) ) {
		if ( mkdir( c, 0775 ) < 0 ) {
		    syslog( LOG_ERR, "Cannot create directory %s: %m", c );
		    printf( "Cannot create directory %s\n", c );
		}
	    }
	    free( c );
	    f = fopen( filename, "w" );	/* Try opening file again */
	}
	return f;
    }
    else {
	syslog( LOG_INFO, "unable to store article %s: %m", filename );
	return NULL;
    }
}

/*
 * calculate first and last article number to get
 * returns 0 for error, 1 for success, -1 if group is not available at all
 */
static int getfirstlast( struct newsgroup *g, unsigned long *first,
    unsigned long *last ) {
    unsigned long h, window;
    long n;
    char * l;

    putaline( "GROUP %s", g->name );
    l = getaline( nntpin );
    if ( !l )
	return FALSE;

    if ( get_long( l, &n ) && n == 480 ) {
	if ( authenticate() )
	    putaline( "GROUP %s", g->name );
	else
	    return -1;
    }

    if ( get_long( l, &n ) && n == 411 ) {
	/* group not available on server */
	return -1;
    }

    if (sscanf(l, "%3ld %lu %lu %lu ", &n, &h, &window, last) < 4 || n != 211 )
	return FALSE;

    if ( *last == 0 ) {		/* group available but no articles on server */
	*first = 0;
	return FALSE;
    }

    if ( extraarticles ) {
	h = *first - extraarticles;
	if ( h < window )
	    h = window;
	else if ( h < 1 )
	    h = 1;
	if ( verbose > 1 )
	    printf( "backing up from %lu to %lu\n", h, *first );
	*first = h;
    }

    if ( *first > (*last+1) ) {
	syslog( LOG_INFO,
		"%s: last seen article was %lu, server now has %lu-%lu",
		g->name, *first, window, *last );
	if ( *first > (*last+5) ) {
	    if ( verbose )
		printf( "%s: switched upstream servers? %lu > %lu\n",
			g->name, *first-1, *last );
	    *first = window;	/* check all upstream articles again */
	}
	else {
	    if ( verbose )
		printf( "%s: rampant spam cancel? %lu > %lu\n",
			g->name, *first-1, *last );
	    *first = *last - 5;	/* re-check last five upstream articles */
	}
    }

    if ( (*first == 1) && initiallimit && (*last-*first > initiallimit) ) {
	if ( verbose > 1 )
	    printf( "skipping articles %lu-%lu inclusive (initial limit)\n",
		    *first, *last-initiallimit );
	syslog( LOG_INFO, "skipping articles %lu-%lu inclusive (initial limit)",
		*first, *last-initiallimit );
	*first = *last-initiallimit+1;
    }

    if ( artlimit && (*last-*first ) > artlimit ) {
	if ( verbose > 1 )
	    printf( "skipping articles %lu-%lu inclusive (article limit)\n",
		    *first, *last-artlimit );
	syslog( LOG_INFO, "skipping articles %lu-%lu inclusive (article limit)",
		*first, *last-artlimit );
	*first = *last-artlimit+1;
    }

    if ( window < *first )
	window = *first;
    if ( window < 1 )
	window = 1;
    *first = window;

    if ( *first > *last ) {
	if ( verbose > 1 )
	    printf( "%s: no new articles\n", g->name );
	return FALSE;
    }

    return 1;
}

/*
 * get headers of articles with XOVER and return a stringlist of article
 * numbers to get
 */
static int doxover( struct stringlist ** stufftoget,
		    unsigned long first, unsigned long last,
		    struct filterlist * filter, char * groupname ) {
    char * l;
    unsigned long count = 0;
    const char *c;
    long reply;
    struct stringlist * helpptr = NULL;

    putaline( "XOVER %lu-%lu", first, last );
    l = getaline( nntpin );
    if ( (!get_long(l, &reply )) || ( reply != 224 ) ) {
	syslog( LOG_NOTICE, "Unknown reply to XOVER command: %s", l );
	printf( "Unknown reply to XOVER command: %s\n", l );
	return -1;
    }
    debug = 0;
    while ( (l = getaline( nntpin )) && strcmp( l, "." ) ) {
	int xoverlen;
	char * artno;
	char * subject;
	char * from;
	char * date;
	char * messageid;
	char * references;
	char * lines;
	char * bytes;
	char * newsgroups = NULL;
	char * p, * q;

	/* format of an XOVER line is usually:
	   article number, subject, author, date, message-id, references,
	   byte count, line count, xhdr (optional) */
	/* to check whether this is correct, one should do a LIST OVERVIEW.FMT
	   before */
	xoverlen = strlen( l );
	p = strchr( l, '\t' );
	if ( p && *p )
	    *p++ = '\0';
	artno = l;
	q = strchr( p, '\t' );
	if ( q && *q )
	    *q++ = '\0';
	subject = p;
	p = q;
	q = strchr( p, '\t' );
	if ( q && *q )
	    *q++ = '\0';
	from = p;
	p = q;
	q = strchr( p, '\t' );
	if ( q && *q )
	    *q++ = '\0';
	date = p;
	p = q;
	q = strchr( p, '\t' );
	if ( q && *q )
	    *q++ = '\0';
	messageid = p;
	p = q;
	q = strchr( p, '\t' );
	if ( q && *q )
	    *q++ = '\0';
	references = p;
	p = q;
	if ( !p ) {
	     /* this happens with References: lines that are too long;
	        generated e.g. by Netscape Collabra at news.mozilla.org
		in that case we generate guesstimates for average articles
	      */
	    bytes = NULL;
	    lines = NULL;
	}
	else {
	    q = strchr( p, '\t' );
	    if ( q && *q )
		*q++ = '\0';
	    bytes = p;
	    p = q;
	    q = strchr( p, '\t' );
	    if ( q && *q )
		*q++ = '\0';
	    lines = p;
	}

	/* is there an Xref: header present as well? */
	p = q;
	if ( p == NULL || *p == '\0' ) {
	    /* no Xref: header */
	    newsgroups = groupname;
	}
	else {
	    q = strchr( p, '\t' );
	    if ( q && *q )
		*q = '\0';
	    if ( strncasecmp( p, "Xref:", 5 ) != 0 ) {
		/* something here, but it's no Xref: header */
		newsgroups = groupname;
	    }
	}
	if ( !newsgroups ) {
	    /* Xref: header is present and must be parsed */
	    p += 5;
	    q = p;
	    while ( q && *q && isspace((int)*q) )
		q++;
	    if ( q == NULL || *q == '\0' ) {
		/* Xref: header but no newsgroups given */
		newsgroups = groupname;
	    }
	    else {
		/* Allocating space for pseudo-newsgroups header */
		newsgroups = critmalloc( strlen(p),
			     "Allocating space for newsgroups header" );
		memset( newsgroups, 0, strlen(p) );
		while ( q && *q && !isspace((int)*q) )
		    q++;			/* skip hostname */
		while ( q && *q && isspace((int)*q) )
		    q++;
		p = q;
		/* now the thing points to the first newsgroup, if present */

		while ( p && *p ) {
		    q = strchr( p, ':' );
		    *q = '\0';
		    strcat( newsgroups, p );
		    p = q+1;
		    while ( isdigit((int)*p) )
			p++;			/* skip article number */
		    while ( isspace((int)*p) )
			p++;
		    if ( p && *p )		/* more than one newsgroup */
			strcat( newsgroups, "," );
		}
	    }
	}

	if ( ( filtermode & FM_XOVER ) || delaybody ) {
	    char *hdr;

	    hdr = critmalloc( xoverlen + strlen(newsgroups) + 200,
			      "allocating space for pseudo header" );
	    sprintf( hdr, "From: %s\n"
			  "Subject: %s\n"
			  "Message-ID: %s\n"
			  "References: %s\n"
			  "Date: %s\n"
			  "Newsgroups: %s\n",
			  from, subject, messageid, references, date,
			  newsgroups );
	    if ( lines ) {
		strcat( hdr, "Lines: " );
		strcat( hdr, lines );
		strcat( hdr, "\n" );
	    }
	    if ( bytes ) {
		strcat( hdr, "Bytes: " );
		strcat( hdr, bytes );
		strcat( hdr, "\n" );
	    }
	    if ( killfilter(filter, hdr) ) {
		groupkilled++;
		/* filter pseudoheaders */
		free( hdr );
		continue;
	    }
	    c = lookup( messageid );
	    if ( c ) {
		/* no good but this server is for small sites */
		struct stat st;
		if ( stat( c, &st ) == 0 )
		    /* we have the article already */
		    continue;
	    }
	    if ( delaybody ) {
		/* write pseudoarticle */
		FILE *f;

		if ( !c ) {
		    syslog( LOG_ERR, "lookup of %s failed", messageid );
		    free( hdr );
		    continue;
		}
		if ( ( f = fopenmsgid( c, FALSE ) ) != NULL ) {
		    fprintf( f, "%s", hdr );
		    fprintf( f, "\n[ Thread has been marked for download ]\n" );
		    fclose( f );
		    store( c, f, newsgroups, messageid );
		}
	    }
	    else {
		count++;
		appendtolist( stufftoget, &helpptr, artno );
	    }
	    free( hdr );
	}
	else {
	    count++;
	    appendtolist( stufftoget, &helpptr, artno );
	}
    }
    debug = debugmode;

    if ( strcmp( l, "." ) == 0 )
	return count;
    else
	return -1;
}

/*
 * use XHDR to check which articles to get. This is faster than XOVER
 * since only message-IDs are transmitted, but you lose some features
 */
static int doxhdr( struct stringlist ** stufftoget,
                   unsigned long first, unsigned long last ) {
    char * l;
    unsigned long count = 0;
    const char * c;
    long reply;
    struct stringlist * helpptr = NULL;

    putaline( "XHDR message-id %lu-%lu", first, last );
    l = getaline( nntpin );
    if ( ( !get_long( l, &reply )) || ( reply != 221 ) ) {
        syslog( LOG_NOTICE, "Unknown reply to XHDR command: %s", l );
        printf( "Unknown reply to XHDR command: %s\n", l );
        return -1;
    }
    debug = 0;
    while ( (l = getaline( nntpin )) && strcmp( l, "." ) ) {
	/* format is: [# of article] [message-id] */
	char *t;

	t = l;
	while ( !isspace((unsigned char)*t) )
	    t++;
	while ( isspace((unsigned char)*t) ) {
	    *t = '\0';
	    t++;
	}
	c = lookup( t );
	if ( c ) {
            /* no good but this server is for small sites */
            struct stat st;
            if ( stat( c, &st ) == 0 )
            /* we have the article already */
                continue;
        }
	/* mark this article */
	count++;
	appendtolist( stufftoget, &helpptr, l );
    }
    debug = debugmode;

    if ( strcmp( l, "." ) == 0 )
        return count;
    else
	return -1;
}

/*
 * get articles
 */

/*
 * Check whether the relevant headers are present. Return TRUE if yes,
 * FALSE if not
 */
static int legalheaders( char * hdr, unsigned long artno ) {
    char *p, *q;
    int havepath, havemsgid, havefrom, haveng;

    havepath = havemsgid = havefrom = haveng = FALSE;
    p = hdr;
    while ( p && *p && ( (q = strchr( p, '\n' )) != NULL) ) {
	if ( !havepath && strncasecmp( p, "Path:", 5 ) == 0 )
	    havepath = TRUE;
	if ( !havemsgid && strncasecmp( p, "Message-ID:", 11 ) == 0 )
	    havemsgid = TRUE;
	if ( !havefrom && strncasecmp( p, "From:", 5 ) == 0 )
	    havefrom = TRUE;
	if ( !haveng && strncasecmp( p, "Newsgroups:", 11 ) == 0 )
	    haveng = TRUE;
	p = q + 1;
    }
    if ( !havepath )
	syslog( LOG_INFO, "Discarding article %lu - no Path: header", artno );
    else if ( !havemsgid )
	syslog( LOG_INFO, "Discarding article %lu - no Message-ID: header",
		artno );
    else if ( !havefrom )
	syslog( LOG_INFO, "Discarding article %lu - no From: header", artno );
    else if ( !haveng )
	syslog( LOG_INFO, "Discarding article %lu - no Newsgroups: header",
		artno );
    return ( havepath && havemsgid && havefrom && haveng );
}

/*
 * get a single article (incl. filtering), return article number
 */
unsigned long getarticle( struct filterlist *filter ) {
    char *l;
    char *msgid, *ssmid, *newsgroups;
    const char *filename;
    char *h = NULL;
    FILE *f = NULL;
    unsigned long hsize = 0;
    unsigned long hlen = 0;
    int reply = 0;
    unsigned long artno;

    l = getaline( nntpin );
    if ( ( sscanf( l, "%3d %lu", &reply, &artno ) != 2 ) ||
         ( reply/10 != 22 ) ) {
	if ( verbose )
	    printf( "Wrong reply to ARTICLE command: %s\n", l );
	syslog( LOG_NOTICE, "Wrong reply to ARTICLE command: %s", l );
	return 0;
    }

    /* get headers, do filtering */
    h = critmalloc( 1024, "Allocating space for headers" );
    *h = '\0';
    hsize = 1024;
    debug = 0;
    while ( ( l = getaline( nntpin ) ) && *l ) {
	hlen = hlen + strlen(l) + 1;
	while ( hlen > hsize ) {
	    h = critrealloc( h, hsize+1024, "Reallocating space for headers" );
	    hsize += 1024;
	}
	if ( strncmp( l, "Xref:", 4 ) ) {
	    strcat( h, l );
	    strcat( h, "\n" );
	}
    }
    debug = debugmode;

    if ( !legalheaders(h, artno) ||
	 ( (FM_HEAD & filtermode) && killfilter(filter, h) ) ) {
	_ignore_answer( nntpin );
	free( h );
	groupkilled++;
	return artno;
    }

    /* get body */
    msgid = mgetheader( "Message-ID:", h );
    newsgroups = mgetheader( "Newsgroups:", h );
    filename = lookup(msgid);
    if ( !msgid || !newsgroups || !filename ) {
	_ignore_answer( nntpin );
	free( h );
	groupkilled++;
	return artno;
    }
    if ( (f = fopenmsgid( filename, FALSE )) == NULL ) {
	_ignore_answer( nntpin );
	free( h );
	groupkilled++;
	return artno;
    }

    fprintf( f, "%s", h );
    free( h );

    /* create Xref: headers and crossposts */
    store( filename, f, newsgroups, msgid );
    fprintf( f, "\n" );	/* empty line between header and body */

    debug = 0;
    while ( ( (l = getaline( nntpin )) != NULL ) && strcmp( l, "." ) ) {
	if ( *l && *l == '.' )
	    fprintf( f, "%s\n", l+1 );
	else
	    fprintf( f, "%s\n", l );
    }
    debug = debugmode;
    fclose( f );
    groupfetched++;

    if ( ( ssmid = getheader( filename, "Supersedes:" ) ) ) {
	supersede( ssmid );	/* supersede article unconditionally;
				   this is not a good idea */
	free( ssmid );
    }

    free( newsgroups );
    free( msgid );

    if ( l )
	return artno;
    else
	return 0;		/* error receiving article */
}

/*
 * get all articles in a group, with overlapping NNTP commands
 */
static unsigned long getarticles( struct stringlist * stufftoget,
				  long n, struct filterlist *f ) {
    struct stringlist * p;
    long sent, window, recd;
    unsigned long server = 0;

    p = stufftoget;
    sent = 0; recd = 0;
    window = ( n < windowsize ) ? n : windowsize;
    while ( ( sent < window ) && ( p != NULL ) ) {
	putaline( "ARTICLE %s", p->string );
	p = p->next;
	sent++;
	if ( throttling )
	    sleep( throttling );
    }

    while ( ( sent < n ) && ( p != NULL ) ) {
	server = getarticle(f);
	recd++;
	putaline( "ARTICLE %s", p->string );
	p = p->next;
	sent++;
	if ( throttling )
	    sleep( throttling );
    }

    while ( recd < sent ) {
	server = getarticle( f );
	recd++;
    }

    return server;
}

/*
 * getgroup():
 * returns 0 if group is unavailable, otherwise last article number in that
 * group
 */
unsigned long getgroup( struct newsgroup * g, unsigned long first ) {
    struct stringlist * stufftoget = NULL;
    struct filterlist * f = NULL;
    long x = 0;
    long outstanding = 0;
    unsigned long last = 0;

    /* lots of plausibility tests */
    if ( !g )
	return first;
    if ( ! isinteresting( g->name ) )
	return 0;

    groupfetched = 0;
    groupkilled  = 0;

    /* get marked articles first */
    if ( delaybody && ( headerbody != 1 ) ) {
	getmarked( g );
	if ( headerbody == 2 ) {
	    /* get only marked bodies, nothing else */
	    return first;
	}
    }

    if ( !first )
	first = 1;
    if ( g->first > g->last )
	g->last = g->first ;
    (void) chdirgroup( g->name, TRUE );	/* to create the directory */

    x = getfirstlast( g, &first, &last );
    switch ( x ) {
	case 0 : { return first; break; }
	case -1: { return 0;     break; }
	default: { break; }
    }

    if ( debugmode )
	syslog( LOG_INFO, "%s: considering articles %lu-%lu", g->name,
		first, last );
    if ( verbose > 1 )
	printf( "%s: considering articles %lu-%lu\n", g->name, first, last );

    if ( filter )
	f = selectfilter( g->name );
    /* use XOVER if filters are used or headers only requested, otherwise
       XHDR to save traffic */
    if ( f || delaybody )
	outstanding = doxover( &stufftoget, first, last, f, g->name );
    else
	outstanding = doxhdr( &stufftoget, first, last );

    switch ( outstanding ) {
	case -1: { return first; break; }	/* error; consider again */
	case  0: { return last; break; }	/* all articles already here */
	default: { break; }
    }
    if ( delaybody )
	return last;

    syslog( LOG_INFO, "%s: will fetch %lu articles", g->name, outstanding );
    getarticles( stufftoget, outstanding, f );
    freefilter(f);
    freelist( stufftoget );

    syslog( LOG_INFO, "%s: %lu articles fetched (to %lu), %lu killed",
	    g->name, groupfetched, g->last, groupkilled );
    globalfetched += groupfetched; globalkilled += groupkilled;
    if ( verbose > 1 )
	printf( "%s: %lu articles fetched, %lu killed\n",
		g->name, groupfetched, groupkilled );
    return(last+1);
}

/*
 * get active file from current_server
 */
static void nntpactive( time_t update ) {
    struct stat st;
    char * l, * p;
    struct stringlist * groups = NULL;
    struct stringlist * helpptr = NULL;
    char timestr[64];
    int reply = 0, error;

    if ( !current_server->port )
	sprintf( s, "%s/leaf.node/%s", spooldir, current_server->name );
    else
	sprintf( s, "%s/leaf.node/%s:%d", spooldir, current_server->name,
		 current_server->port );

    if ( active && !forceactive && ( stat( s, &st ) == 0 ) ) {
	if ( verbose )
	    printf( "Getting new newsgroups from %s\n", current_server->name );
	/* to avoid a compiler warning we print out a four-digit year;
	 * but since we need only the last two digits, we skip them
	 * in the next line
	 */
	strftime( timestr, 64, "%Y%m%d %H%M00", gmtime( &update ) );
	putaline( "NEWGROUPS %s GMT", timestr+2 );
	if ( nntpreply() != 231 ) {
	    printf( "Reading new newsgroups failed.\n" );
	    syslog( LOG_ERR, "Reading new newsgroups failed" );
	    return;
	}
	while ( (l=getaline(nntpin)) && ( *l != '.' ) ) {
	    p = l;
	    while (!isspace((unsigned char)*p) )
		p++;
	    if (*p)
		*p = '\0';
	    insertgroup( l, 0, 0, time(NULL), NULL );
	    appendtolist( &groups, &helpptr, l );
	}
	if ( !l )		/* timeout */
	    return;
	mergegroups();		/* merge groups into active */
	helpptr = groups;
	while ( helpptr != NULL ) {
	    if ( current_server->descriptions ) {
		error = 0;
		putaline( "XGTITLE %s", helpptr->string );
		reply = nntpreply();
		if ( reply != 282 ) {
		    putaline( "LIST NEWSGROUPS %s", helpptr->string );
		    reply = nntpreply();
		    if ( reply && ( reply != 215 ) )
			error = 1;
		}
		if ( !error ) {
		    l = getaline(nntpin);
		    if ( l && *l && *l != '.' ) {
			p = l;
			while ( !isspace((unsigned char)*p) )
			    p++;
			while ( isspace((unsigned char)*p) ) {
		            *p = '\0';
		    	    p++;
			}
			if ( reply == 215 || reply == 282 )
			    changegroupdesc( l, *p ? p : NULL );
			do {
			    l = getaline(nntpin);
			    error++;
			} while ( l && *l && *l != '.' );
			if ( error > 1 ) {
			    current_server->descriptions = 0;
			    syslog( LOG_NOTICE, "warning: %s does not process "
				   "LIST NEWSGROUPS %s correctly: use nodesc\n",
				   current_server->name, helpptr->string );
			    printf("warning: %s does not process LIST "
				   "NEWSGROUPS %s correctly: use nodesc\n",
				   current_server->name, helpptr->string );
			}
		    }
		}
	    } /* if ( current_server->descriptions ) */
	    helpptr = helpptr->next;
	}
	freelist( groups );
    } else {
	if ( verbose )
	    printf( "Getting all newsgroups from %s\n", current_server->name );
	putaline( "LIST" );
	if ( nntpreply() != 215 ) {
	    printf( "Reading all newsgroups failed.\n" );
	    syslog( LOG_ERR, "Reading all newsgroups failed" );
	    return;
	}
	debug = 0;
	while ( ( l = getaline( nntpin ) ) && ( strcmp( l, "." ) ) ) {
            p = l;
            while (!isspace((unsigned char)*p) )
                p++;
            while ( isspace((unsigned char)*p) ) {
                *p = '\0';
                p++;
            }
            insertgroup( l, 0, 0, 0, NULL );
        }
	mergegroups();
	debug = debugmode;
	if ( !l )		/* timeout */
	    return;
	putaline( "LIST NEWSGROUPS" );
	l = getaline( nntpin );
	/* correct reply starts with "215". However, INN 1.5.1 is broken
	   and immediately returns the list of groups */
	if ( l ) {
	    char *p;
	    reply = strtol( l, &p, 10 );
	    if ( ( reply == 215 ) && ( *p == ' ' || *p == '\0' ) )
		l = getaline( nntpin ); /* get first description */
	    else if ( *p != ' ' && *p != '\0' )
		/* INN 1.5.1: line already contains description */
		;
	    else {
		printf( "Reading newsgroups descriptions failed: %s.\n", l );
		syslog( LOG_ERR, "Reading newsgroups descriptions failed: %s",
			l );
		return;
	    }
	}
	else {
	    printf( "Reading newsgroups descriptions failed.\n" );
	    syslog( LOG_ERR, "Reading newsgroups descriptions failed" );
	    return;
	}
	debug = 0;
	while ( l && ( strcmp( l, "." ) ) ) {
	    p = l;
	    while (!isspace((unsigned char)*p) )
		p++;
	    while ( isspace((unsigned char)*p) ) {
                *p = '\0';
                p++;
            }
	    changegroupdesc( l, *p ? p : NULL );
	    l = getaline(nntpin);
	}
	debug = debugmode;
	if ( !l )
	    return;		/* timeout */
    }
}


/*
 * post all spooled articles
 *
 * if all postings succeed, returns 1
 * if there are no postings to post, returns 1
 * if a posting is strange for some reason, returns 0
 */
int postarticles( void ) {
    struct stat st;
    char * line;
    DIR * d;
    struct dirent * de;
    FILE * f;
    int r, haveid, n;

    n = 0;

    if ( chdir( spooldir ) || chdir ( "out.going" ) ) {
	syslog( LOG_ERR, "Unable to cd to outgoing directory: %m" );
	printf( "Unable to cd to %s/out.going\n", spooldir );
	return 1;
    }

    d = opendir( "." );
    if ( !d ) {
	syslog( LOG_ERR, "Unable to opendir out.going: %m" );
	printf( "Unable to opendir %s/out.going\n", spooldir );
	return 1;
    }

    while ( (de=readdir( d )) != NULL ) {
	haveid = 0;
	if ( ( stat( de->d_name, &st ) == 0 ) && S_ISREG( st.st_mode ) &&
	     ( ( f = fopen( de->d_name, "r" ) ) != NULL ) &&
	     isgrouponserver( fgetheader( f, "Newsgroups:" ) ) &&
	     ( ( haveid = 
		 ismsgidonserver( fgetheader( f, "Message-ID:" ) ) ) != 1 ) ) {
	    if ( verbose > 2 )
		printf( "Posting %s...\n", de->d_name );
	    syslog( LOG_INFO, "Posting %s...",de->d_name );
	    putaline( "POST" );
	    r = nntpreply();
	    if ( r != 340 ) {
		if ( verbose )
		    printf( "Posting %s failed: %03d reply to POST\n",
			     de->d_name, r );
		sprintf( s, "%s/failed.postings/%s", spooldir, de->d_name );
		syslog( LOG_ERR, "unable to post (%d), moving to %s", r, s );
		if ( link( de->d_name, s ) )
		    syslog( LOG_ERR, "unable to move failed posting to %s: %m",
				      s );
	    } else {
	        debug = 0;
		while ( ( line = getaline( f ) ) != NULL ) {
		    /* can't use putaline() here because
		       line length is restricted to 1024 bytes in there */
		    if ( strlen(line) && line[0] == '.' )
			fprintf( nntpout, ".%s\r\n", line );
		    else
			fprintf( nntpout, "%s\r\n", line );
		};
		fflush( nntpout );
		debug = debugmode;
		putaline( "." );
		line = getaline( nntpin );
		if ( !line ) {		/* timeout: posting failed */
		    fclose( f );
		    closedir( d );
		    return 0;
		}
		if ( strncmp( line, "240", 3 ) == 0 ) {
		    if ( verbose > 2 )
			printf( " - OK\n" );
		    n++;
		} else {
		    if ( verbose )
			printf( "Article %s rejected: %s\n",
				de->d_name, line );
		    sprintf( s, "%s/failed.postings/%s", 
			     spooldir, de->d_name );
		    syslog( LOG_ERR, 
			    "Article %s rejected (%s), moving to %s",
			    de->d_name, line, s );
		    if ( link( de->d_name, s ) )
			syslog( LOG_ERR,
				"unable to link failed posting to %s: %m", s );
		} 
	    }
	    fclose( f );
	    haveid = 0;
	} else if ( haveid ) {
	    syslog( LOG_INFO, "Message-ID of %s already in use upstream -- "
			"article discarded\n", de->d_name );
	    if ( verbose > 2 )
		printf( "%s already available upstream\n", de->d_name );
	    unlink( de->d_name ); 
	}
    }
    closedir( d );
    if ( verbose && n )
	syslog( LOG_INFO, "%s: %d articles posted", current_server->name, n );
    return 1;
}

static void processupstream( const char * server, const int port ) {
    FILE * f;
    DIR * d;
    struct dirent * de;
    struct newsgroup * g;
    int havefile;
    unsigned long newserver = 0;
    char * l;
    char * oldfile ;
    struct stringlist * ngs = NULL;
    struct stringlist * helpptr = NULL;

    /* read server info */
    if ( !port )
	sprintf( s, "%s/leaf.node/%s", spooldir, server );
    else
	sprintf( s, "%s/leaf.node/%s:%d", spooldir, server, port );
    oldfile = strdup( s );
    havefile = 0;
    if ( ( f = fopen( s, "r" ) ) != NULL ) {
	/* a sorted array or a tree would be better than a list */
	ngs = NULL;
	debug = 0;
        if ( verbose > 1 )
            printf( "Read server info from %s\n", s );
	while ( ( ( l = getaline( f ) ) != NULL ) && ( strlen(l) ) ) {
	    appendtolist( &ngs, &helpptr, l );
	}
	havefile = 1;
	debug = debugmode;
	fclose( f );
    }

    sprintf( s, "%s/interesting.groups", 
	     spooldir );
    d = opendir( s );
    if ( !d ) {
	syslog( LOG_ERR, "opendir %s: %m", s );
	return;
    }

    if ( !port )
	sprintf( s, "%s/leaf.node/%s~", spooldir, server );
    else
	sprintf( s, "%s/leaf.node/%s:%d~", spooldir, server, port );
    f = fopen( s, "w" );
    if ( f == NULL ) {
	syslog( LOG_ERR, "Could not open %s for writing.", s );
        printf( "Could not open %s for writing.\n", s );
    }
    while ( (de=readdir(d)) ) {
	if ( isalnum((unsigned char)*(de->d_name)) ) {
	    g = findgroup( de->d_name );
	    if ( g != NULL ) {
		sprintf( s, "%s ", g->name );
		l = havefile ? findinlist( ngs, s ) : NULL;
		if ( l && *l ) {
		    char * t;
		    unsigned long a;
		    t = strchr( l, ' ' );
		    if ( t == NULL || *t == '\0' )
		        newserver = getgroup( g, 1 );
		    else {
			a = strtoul( t, NULL, 10 );
			if ( a )
			    newserver = getgroup( g, a );
			else
			    newserver = getgroup( g, 1 );
		    }
		}
		else
		    newserver = getgroup( g, 1 );
		if ( ( f != NULL ) && newserver )
                    fprintf( f, "%s %lu\n", g->name, newserver );
            } else {
		printf( "%s not found in groupinfo file\n", de->d_name );
		syslog( LOG_INFO, "%s not found in groupinfo file",
			de->d_name );
	    }
	}
    }
    closedir( d );
    if ( f != NULL ) {
        fclose(f);
	if ( !port )
	    sprintf( s, "%s/leaf.node/%s~", spooldir, server );
	else
	    sprintf( s, "%s/leaf.node/%s:%d~", spooldir, server, port );
	rename( s, oldfile );
    }

    free( oldfile );
    freelist(ngs);
}

/*
 * checks whether all newsgroups have to be retrieved anew
 * returns 0 if yes, time of last update if not
 */
static int checkactive( void ) {
    struct stat st;

    sprintf( s, "%s/active.read", spooldir );
    if ( stat( s, &st ) )
	return 0;
    if ( ( now - st.st_mtime ) < ( timeout_active * SECONDS_PER_DAY ) ) {
	if ( debugmode )
	    syslog( LOG_DEBUG,
		    "Last LIST ACTIVE done %ld seconds ago: NEWGROUPS\n",
		    (long)(now - st.st_mtime));
	return ( now < st.st_atime ) ? now : st.st_atime ;
    } else {
	if ( debugmode )
	    syslog( LOG_DEBUG, "Last LIST ACTIVE done %ld seconds ago: LIST\n",
		    (long)(now - st.st_mtime));
	return 0;
    }
}

static void updateactive( void ) {
    struct stat st;
    struct utimbuf buf;
    FILE * f;

    sprintf( s, "%s/active.read", spooldir );
    if ( stat( s, &st ) ) {
	/* active.read doesn't exist */
	if ( ( f = fopen( s, "w" ) ) != NULL )
	    fclose( f );
    } else {
	if ( now < st.st_atime ) {
	    /* This may happen through hardware failures */
	    syslog( LOG_NOTICE,
		    "active.read touched in the future: check atime and date" );
	    buf.actime = st.st_atime ;
	}
	else
	    buf.actime = now;
	if ( (now - st.st_mtime) < (timeout_active*SECONDS_PER_DAY) )
	    buf.modtime = st.st_mtime;
	else
	    buf.modtime = now;
	if ( utime( s, &buf ) ) {
	    if ( ( f = fopen( s, "w" ) ) != NULL )
		fclose( f );
	    else {
		printf("Unable to access %s/active.read\n", spooldir );
		syslog( LOG_NOTICE,
			"Unable to access %s/active.read", spooldir );
	    }
	}
    }
}

/*
 * get a command line parameter as an int
 */
static int getparam( char *arg ) {
    char *c = NULL;
    int value;

    if ( !arg ) {
	usage();
	exit(EXIT_FAILURE);
    }
    value = strtol( arg, &c, 10 );
    if ( c && !*c )
	return( value );
    else {
	usage();
	exit(EXIT_FAILURE);
    }
}

/*
 * works current_server. Returns TRUE if no other servers have to be queried,
 * FALSE otherwise
 */
static int do_server( char *msgid, time_t lastrun ) {
    int reply;

    if ( verbose )
	printf( "Trying to connect to %s ... ", current_server->name );
    fflush( stdout );
    reply = nntpconnect( current_server );
    if ( reply ) {
	if ( verbose )
	    printf("connected.\n");
	if ( current_server->username )
	    ( void ) authenticate();
	putaline( "MODE READER" );
	reply = nntpreply();
	if ( reply == 498 )
	    syslog( LOG_INFO, "Connection refused by %s",
		    current_server->name );
	else if ( msgid ) {
	    /* if retrieval of the message id is successful at one
	       server, we don't have to check the others */
	    if ( getbymsgid( msgid ) ) {
		return TRUE;
	    }
	}
	else {
	    if ( reply == 200 )
		( void ) postarticles();
	    if ( !postonly ) {
		nntpactive( lastrun );
			/* get list of newsgroups or new newsgroups */
		processupstream( current_server->name, current_server->port );
	    }
	}
	putaline( "QUIT"); /* say it, then just exit :) */
	nntpdisconnect();
	if ( verbose )
	    printf( "Disconnected from %s.\n", current_server->name );
    }
    else {
	if ( verbose )
	    printf("failed.\n");
    }
    return FALSE;
}

int main( int argc, char ** argv ) {
    int option, reply, flag;
    time_t starttime;
    volatile time_t lastrun;
    char * conffile;
    static char * msgid = NULL;
    char * newsgroup = NULL;

    verbose = 0;
    postonly = 0;
    conffile = critmalloc( strlen(libdir) + 10,
			   "Allocating space for configuration file name" );
    sprintf( conffile, "%s/config", libdir );

    if ( !initvars( argv[0] ) )
	exit(EXIT_FAILURE);

#ifdef HAVE_OLD_SYSLOG
    openlog( "fetchnews", LOG_PID );
#else
    openlog( "fetchnews", LOG_PID|LOG_CONS, LOG_NEWS );
#endif

    starttime = time( NULL );
    now       = time( NULL );
    umask(2);

    whoami();

    servers = NULL;

    conffile = getoptarg( 'F', argc, argv );   
    if ( ( reply = readconfig( conffile ) ) != 0 ) {
	printf("Reading configuration failed (%s).\n", strerror(reply) );
	exit( 2 );
    }
/* is this sensible?
    if ( findopt( 'D', argc, argv ) )
	debugmode = 0;
*/

    flag = FALSE;
    while ( (option=getopt( argc, argv, "VDHBPF:S:N:M:fnvx:p:t:" )) != -1 ) {
	if ( parseopt( "fetchnews", option, optarg, NULL ) ) {
	    ;
	} else if ( option == 't' ) {
	    throttling = getparam( optarg );
	} else if ( option == 'x' ) {
	    extraarticles = getparam( optarg );
	} else if ( ( option == 'S' ) && optarg && strlen( optarg ) ) {
	    struct serverlist * sl;
	    char * p;

	    if ( !flag ) {
		/* deactive all servers but don't delete them */
		sl = servers;
		while ( sl ) {
		    sl->active = FALSE;
		    sl = sl->next;
		}
	        flag = TRUE;
	    }
	    sl = findserver(optarg);
	    if ( sl ) {
		sl->active = TRUE;
	    }
	    else {
		/* insert a new server in serverlist */
		sl = (struct serverlist *)critmalloc(sizeof(struct serverlist),
		      "allocating space for server name" );
		sl->name = strdup( optarg );
		/* if port definition is present, cut it off */
		if ( ( p = strchr( sl->name, ':' ) ) != NULL ) {
		    *p = '\0';
		}
		sl->descriptions = TRUE;
		sl->next = servers;
		sl->timeout = 30;	/* default 30 seconds */
		sl->port = 0;		/* default port */
		/* if there is a port specification, override default: */
		if ( ( p = strchr( optarg, ':' ) ) != NULL ) {
		    p++;
		    if ( p && *p ) {
			sl->port = strtol( p, NULL, 10 );
		    }
		}
		sl->username = NULL;
		sl->password = NULL;
		sl->active = TRUE;
		servers = sl;
	    }
	} else if ( ( option == 'N' ) && optarg && strlen( optarg ) ) {
	    newsgroup = strdup( optarg );
	} else if ( ( option == 'M' )  && optarg && strlen( optarg ) ) {
	    msgid = strdup( optarg );
        } else if ( option == 'n' ) {
            noexpire = 1;
	} else if ( option == 'f' ) {
	    forceactive = 1;
	} else if ( option == 'P' ) {
	    if ( !msgid )
		postonly = 1;
	} else if ( option == 'H' ) {
	    if ( !msgid && testheaderbody( option ) )
		headerbody = 1;
	} else if ( option == 'B' ) {
	    if ( !msgid && testheaderbody( option ) )
		headerbody = 2;
	} else {
	    usage();
	    exit(EXIT_FAILURE);
	}
	debug = debugmode;
    }

    /* check compatibility of options */
    if ( msgid ) {
	if ( delaybody ) {
	    fprintf( stderr, "Option -M used: "
		     "ignored configuration file setting \"delaybody\".\n" );
	    delaybody = 0;
	}
	if ( headerbody ) {
	    fprintf( stderr, "Option -M used: ignored options -H or -B.\n" );
	    headerbody = 0;
	}
	if ( forceactive ) {
	    fprintf( stderr, "Option -M used: ignored option -f.\n" );
	    forceactive = 0;
	}
	if ( postonly ) {
	    fprintf( stderr, "Option -M used: ignored option -P.\n" );
	    postonly = 0;
	}
	if ( newsgroup ) {
	    fprintf( stderr, "Option -M used: ignored option -N.\n" );
	    free( newsgroup );
	    newsgroup = NULL;
	}
    }

    if ( newsgroup ) {
	if ( forceactive ) {
	    fprintf( stderr, "Option -N used: ignored option -f.\n" );
	    forceactive = 0;
	}
	if ( postonly ) {
	    fprintf( stderr, "Option -N used: ignored option -P.\n" );
	    postonly = 0;
	}
    }

    syslog( LOG_DEBUG, "%s: verbosity level is %d; debugging level is %d",
		       version, verbose, debugmode );
    if ( verbose ) {
	printf( "%s: verbosity level is %d\n", version, verbose );
	if ( verbose > 1 && noexpire ) {
	    printf("Do not automatically unsubscribe unread newsgroups.\n");
        }
    }

    /* If fetchnews should post only, no lockfile or filters are required.
     * It is also sensible to check if there is anything to post when
     * invoked with -P; otherwise quit immediately.
     */
    if ( !postonly ) {
	/* Since the nntpd can create a lockfile for a short time, we
	 * attempt to create a lockfile during a five second time period,
	 * hoping that the nntpd will release the lock file during that
	 * time (which it should).
	 */
	if ( signal( SIGALRM, sigcatch ) == SIG_ERR )
	    fprintf( stderr, "Cannot catch SIGALARM.\n" );
	else if ( setjmp( jmpbuffer ) != 0 ) {
	    printf( "lockfile %s exists, abort.\n", lockfile );
	    syslog( LOG_NOTICE, "lockfile %s exists, abort", lockfile );
	    exit( EXIT_FAILURE );
	}
	alarm( 5 );
	if ( lockfile_exists( FALSE, TRUE ) )
	    exit( EXIT_FAILURE );
	alarm( 0 );

	if ( forceactive )
	    fakeactive();
	else
	    readactive();
	readfilter( filterfile );
	if ( !noexpire )
	    checkinteresting();
    }
    else {
	if ( !checkforpostings() )
	    exit(0);
    }

    lastrun = 0;
    if ( !forceactive ) {
	lastrun = checkactive();
	if ( !lastrun )
	    forceactive = 1;
    }

    if ( signal( SIGTERM, sigcatch ) == SIG_ERR )
	fprintf( stderr, "Cannot catch SIGTERM.\n" );
    else if ( signal( SIGINT, sigcatch ) == SIG_ERR )
	fprintf( stderr, "Cannot catch SIGINT.\n" );
    else if ( signal( SIGUSR1, sigcatch ) == SIG_ERR )
	fprintf( stderr, "Cannot catch SIGUSR1.\n" );
    else if ( signal( SIGUSR2, sigcatch ) == SIG_ERR )
	fprintf( stderr, "Cannot catch SIGUSR2.\n" );
    else if ( setjmp( jmpbuffer ) != 0 ) {
	servers = NULL;		/* in this case, jump the while ... loop */
    }

    while ( servers ) {
	current_server = servers;
	if ( current_server->active ) {
	    if ( do_server( msgid, lastrun ) )
		continue;	/* no other servers have to be queried */
	}
	servers = servers->next;
    }

    fflush( stdout );	/* to avoid double logging of stuff */

    switch ( fork() ) {
	case -1: {
	    syslog( LOG_ERR, "fork: %m" );
	    break;
	}
	case  0: {
	    setsid();
	    if ( !postonly ) {
		updateactive();
		writeactive();
		if ( unlink( lockfile ) )
		    syslog( LOG_INFO, "unlocking failed: %m" );
		fixxover();
	    }
	    delposted( starttime );
	    syslog( LOG_DEBUG, "Background process finished" );
	    break;
	}
	default: {
	    if ( !postonly ) {
	        if ( verbose )
		    printf( "%lu articles fetched, %lu killed, in %ld seconds\n",
			globalfetched, globalkilled, time( NULL ) - starttime );
		syslog( LOG_INFO,
			"%lu articles fetched, %lu killed, in %ld seconds",
			globalfetched, globalkilled, time( NULL ) - starttime );
	    }
	    break;
	}
    }

    exit(0);
}
