/*
libutil -- miscellaneous stuff

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
Modified by Joerg Dietrich <joerg@dietrich.net>
Copyright of the modifications 2000.

See file COPYING for restrictions on the use of this software.
*/

#include "leafnode.h"
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
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

char fqdn[256];
char s[PATH_MAX+1024]; /* long string, here to cut memory usage */
extern struct state _res;

/*
 * global variables modified here
 */
int debugmode = 0;
int verbose = 0;

int wildmat(const char *text, const char *p);

/*
 * initialize all global variables
 */
int initvars( char * progname ) {
    struct passwd * pw;

    /* config.c stuff does not have to be initialized */

    expire_base = NULL;

    /* These directories should exist anyway */
    sprintf( s, "%s/interesting.groups", spooldir );
    mkdir( s, (mode_t)0775 );
    sprintf( s, "%s/leaf.node", spooldir );
    mkdir( s, (mode_t)0755 );
    sprintf( s, "%s/failed.postings", spooldir );
    mkdir( s, (mode_t)0775 );
    sprintf( s, "%s/out.going", spooldir );
    mkdir( s, (mode_t)2755 );
    chmod( s, (mode_t)(S_ISUID | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) );

    if ( progname ) {
	pw = getpwnam( "news" );
	if ( !pw ) {
	    fprintf( stderr, "no such user: news\n" );
	    return FALSE;
	}

#if defined HAVE_SETGID && defined HAVE_SETUID
	setgid( pw->pw_gid );
	setuid( pw->pw_uid );
	if ( getuid() != pw->pw_uid || getgid() != pw->pw_gid ) {
#else
	setregid( pw->pw_gid, pw->pw_gid );
	setreuid( pw->pw_uid, pw->pw_uid );
	if ( geteuid() != pw->pw_uid || getegid() != pw->pw_gid ) {
#endif
	    fprintf( stderr, "%s: must be run as news or root\n", progname );
	    return FALSE;
	}
    }
    return TRUE;
}

/*
 * Find whether a certain option is specified on the command line
 * and return the pointer to this option - i.e. if 0 is returned,
 * the option is not present. Does not call getopt().
 */
int findopt( char option, int argc, char * argv[] ) {
    int i, j;

    i = 1;
    while ( i < argc ) {
	if ( strcmp( argv[i], "--" ) == 0 ) {	/* end of option args */
	    return 0;
	}
	if ( argv[i][0] == '-' ) {		/* option found */
	    j = strlen(argv[i]) - 1;
	    while ( j > 0 ) {
		if ( argv[i][j] == option )
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
char * getoptarg( char option, int argc, char * argv[] ) {
    int i, j;

    i = findopt( option, argc, argv );
    if ( !i )
	return NULL;
    j = strlen( argv[i] ) - 1;
    if ( (argv[i][j] == option) && (i+1 < argc) && (argv[i+1][0] != '-') ) {
	return argv[i+1] ;
    }
    return NULL;
}

/*
 * parse options global to all leafnode programs
 */
int parseopt( char *progname, char option, char * optarg, char * conffile ) {
    if ( option == 'V' ) {
	printf( "%s %s\n", progname, version );
	exit( 0 );
    }
    else if ( option == 'v' ) {
	verbose++;
	return TRUE;
    }
    else if ( option == 'D' ) {
	debugmode++;
	return TRUE;
    }
    else if ( ( option == 'F' ) && optarg && strlen(optarg) ) {
	if ( conffile )
	    free( conffile );
	conffile = strdup(optarg);
	return TRUE;
    }
    return FALSE;
}

/*
 * check which groups are still interesting and unsubscribe groups
 * which are not
 */
void checkinteresting( void ) {
    time_t now;
    struct dirent *de;
    struct stat st;
    DIR *d;

    now = time( NULL );
    if ( chdir( spooldir ) || chdir( "interesting.groups" ) ) {
	syslog( LOG_ERR, "unable to chdir to %s/interesting.groups: %m",
		spooldir );
	return;
    }

    d = opendir( "." );
    if ( !d ) {
	syslog( LOG_ERR, "unable to opendir %s/interesting.groups: %m",
		spooldir );
	return;
    }

    while ( ( de = readdir( d ) ) != NULL ) {
	if ( ( stat( de->d_name, &st ) == 0 ) && S_ISREG( st.st_mode ) ) {
	    /* reading a newsgroup changes the ctime; if the newsgroup is
	       newly created, the mtime is changed as well */
	    if ( ( ( st.st_mtime == st.st_ctime ) &&
	    	   ( now-st.st_ctime > (timeout_short * SECONDS_PER_DAY) ) ) ||
		 ( now-st.st_ctime > (timeout_long * SECONDS_PER_DAY) ) ) {
		if ( verbose > 1 )
		    printf("skipping %s from now on\n", de->d_name);
		unlink( de->d_name );

		/* don't reset group counts because if a group is
		   resubscribed, new articles will not be shown */
	    }
	}
    }
    closedir( d );
}

/*
 * check if lockfile exists: if yes (and another instance of fetch is
 * still active) return 1; otherwise generate it. If this fails,
 * also return 1, otherwise return 0
 */
int lockfile_exists( int silent, int block ) {
    int fd;
    int ret;
    struct flock fl;

    /* The file descriptor is closed by _exit(2). Very ugly. */
    fd = open( lockfile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR );
    if ( fd == -1 ) {
	if (!silent)
	    printf( "Could not open lockfile %s for writing, "
		    "abort program ...\n", lockfile );
	syslog( LOG_ERR, "Could not open lockfile %s for writing: %m",
		lockfile );
	return 1;
    }
    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    fl.l_pid = getpid();
    ret = fcntl( fd, block ? F_SETLKW : F_SETLK, &fl );
    if ( ret == -1 ) {
	if ( errno == EACCES || errno == EAGAIN ) {
	    if (!silent)
		printf( "lockfile  %s exists, abort ...\n" ,lockfile );
	    syslog( LOG_ERR, "lockfile %s exists, abort ...", lockfile);
	    return 1;
	} else {
	    if (!silent)
		printf( " locking %s failed: %s, abort ...\n"
			, lockfile, strerror( errno ) );
	    syslog( LOG_ERR, "locking %s failed: %m, abort", lockfile );
	    return 1;
	}
    } else {
	char pid[11];  /* Hope nobody has sizeof( pid_t ) > 4 */
	memset( pid, 0, 11 ); 
	snprintf( pid, 11, "%d\n", fl.l_pid );
	write( fd, pid, strlen(pid) ); 
	return 0;
    }
}

/*
 * check whether "groupname" is represented in interesting.groups without
 * touching the file
 */
int isinteresting( const char *groupname ) {
    DIR * d;
    struct dirent * de;
    
    /* Local groups are always interesting. At least for the server :-) */
    if ( islocalgroup( groupname ) )
	return TRUE;

    sprintf( s, "%s/interesting.groups", spooldir );
    d = opendir( s );
    if ( !d ) {
    	syslog( LOG_ERR, "Unable to open directory %s: %m", s );
	printf( "Unable to open directory %s\n", s );
	return FALSE;
    }

    while( ( de = readdir( d ) ) != NULL ) {
    	if ( strcasecmp( de->d_name, groupname ) == 0 ) {
	    closedir( d );
	    return TRUE;
	}
    }
    closedir( d );
    return FALSE;
}

/* no good but this server isn't going to be scalable so shut up */
const char * lookup ( const char *msgid ) {
    static char * name = NULL;
    static unsigned int namelen = 0; 
    unsigned int r;
    unsigned int i;

    if (!msgid || !*msgid)
	return NULL;

    i = strlen(msgid)+strlen(spooldir)+30;

    if (!name) {
	name = (char *)malloc(i);
	namelen = i;
    } else if (i > namelen) {
	name = (char *)realloc(name, i);
	namelen = i;
    }

    if (!name) {
	syslog( LOG_ERR, "malloc(%d) failed", i );
	exit(EXIT_FAILURE);
    }

    strcpy( name, spooldir );
    strcat( name, "/message.id/000/" );
    i = strlen( name );
    strcat( name, msgid );

    r = 0;
    do {
	if ( name[i] == '/' )
	    name[i] = '@';
	else if ( name[i] == '>' )
	    name[i+1] = '\0';
	r += (int)(name[i]);
	r += ++i;
    } while ( name[i] );

    i = strlen( spooldir )+14; /* to the last digit */
    r = (r%999)+1;
    name[i--] = '0'+(char)(r%10); r /= 10;
    name[i--] = '0'+(char)(r%10); r /= 10;
    name[i] = '0'+(char)(r);
    return name;
}

static int makedir( char * d ) {
    char * p;
    char * q;

    if (!d || *d != '/' || chdir("/"))
	return 0;
    q = d;
    do {
	*q = '/';
	p = q;
	q = strchr( ++p, '/' );
	if (q)
	    *q = '\0';
	if (!chdir(p))
	    continue; /* ok, I do use it sometimes :) */
	if (errno==ENOENT)
	    if (mkdir(p, 0775)) {
		syslog( LOG_ERR, "mkdir %s: %m", d );
		exit(EXIT_FAILURE);
	    }
	if (chdir(p)) {
	    syslog( LOG_ERR, "chdir %s: %m", d );
	    exit(EXIT_FAILURE);
	}
    } while ( q );
    return 1;
}
	

/* chdir to the directory of the argument if it's a valid group */
int chdirgroup( const char *group, int creatdir ) {
    char *p;

    if (group && *group) {
	strcpy(s, spooldir);
	p = s + strlen(s);
	*p++ = '/';
	strcpy(p, group);
	while(*p) {
	    if (*p=='.')
		*p = '/';
	    else
		*p = tolower((unsigned char)*p);
	    p++;
	}
	if ( !chdir(s) )
	    return 1;		/* chdir successful */
	if ( creatdir )
	    return makedir( s );
    }
    return 0;
}

/* get the fully qualified domain name of this box into fqdn */

void whoami( void ) {
    struct hostent * he;

    if (!gethostname(fqdn, 255) && (he = gethostbyname(fqdn))!=NULL) {
	strncpy( fqdn, he->h_name, 255 );
	if (strchr(fqdn, '.') == NULL) {
	    char ** alias;
	    alias = he->h_aliases;
	    while( alias && *alias )
		if (strchr(*alias, '.') && (strlen(*alias)>strlen(fqdn)))
		    strncpy( fqdn, *alias, 255 );
		else
		    alias++;
	    }
    } else
	*fqdn = '\0';
}

/*
 * Functions to handle stringlists
 */

/*
 * append string "newentry" to stringlist "list". "lastentry" is a
 * pointer pointing to the last entry in "list" and must be properly
 * intialized.
 */
void appendtolist( struct stringlist ** list, struct stringlist ** lastentry,
    char * newentry ) {

    struct stringlist *ptr;

    ptr = (struct stringlist *)critmalloc( sizeof( struct stringlist ) +
	  strlen( newentry ), "Allocating space in stringlist" );
    strcpy( ptr->string, newentry );
    ptr->next = NULL;
    if ( *list == NULL )
	*list = ptr;
    else
	(*lastentry)->next = ptr;
    *lastentry = ptr;
}

/*
 * find a string in a stringlist
 * return pointer to string if found, NULL otherwise
 */
char * findinlist( struct stringlist * haystack, char * needle ) {
    struct stringlist * a;

    if ( needle == NULL )
	return NULL;
    a = haystack;
    while ( a && a->string ) {
	if ( strncmp( needle, a->string, strlen(needle) ) == 0 )
	    return a->string;
	a = a->next;
    }
    return NULL;
}

/*
 * free a list
 */
void freelist( struct stringlist * list ) {
    struct stringlist * a;

    while ( list ) {
	a = list->next;
	free( list );
	list = a;
    }
}

/*
 * get the length of a list
 */
int stringlistlen( const struct stringlist *list )
{
    int i;
    for ( i = 0; list; i++, list = list->next ) ;
    return i;
}

/*
 * convert a space separated string into a stringlist
 */
struct stringlist * cmdlinetolist( const char *cmdline )
{
    char *c;
    char *o;
    struct stringlist *s = NULL, *l = NULL;

    if ( !cmdline || !*cmdline )
        return NULL;

    o = ( c = critmalloc( strlen( cmdline ) + 1,
                          "Allocating temporary string space" ) );
    
    strcpy( c, cmdline );

    while ( *c ) {
        char *p;
        while ( *c && isspace( (unsigned char)*c ) )
            c++;
        p = c;
        while ( *c && !isspace( (unsigned char)*c ) )
            c++;
        if ( *c )
            *c++ = '\0';
        if ( *p ) /* avoid lists with only one empty entry */
            appendtolist( &s, &l, p );
    }
    free( o );
    return s;
}

/* next few routines implement a mapping from message-id to article
   number, and clearing the entire space */

struct msgidtree {
    struct msgidtree * left;
    struct msgidtree * right;
    int art;
    char msgid[1];
};

static struct msgidtree * head; /* starts as NULL */

void insertmsgid( const char * msgid, unsigned long art ) {
    struct msgidtree ** a;
    int c;

    if ( strchr( msgid, '@' ) == 0 )
	return;

    a = &head;
    while (a) {
	if (*a) {
	    /* comparing only by msgid is uncool because the tree becomes
	       very unbalanced */
	    c = strcmp(strchr((*a)->msgid, '@'), strchr(msgid, '@'));
	    if ( c == 0 )
		c = strcmp((*a)->msgid, msgid);
	    if (c<0)
		a = &((*a)->left);
	    else if (c>0)
		a = &((*a)->right);
	    else {
		return;
	    }
	} else {
	    *a = (struct msgidtree *)
		 critmalloc(sizeof(struct msgidtree) + strlen(msgid),
			    "Building expiry database");
	    (*a)->left = (*a)->right = NULL;
	    strcpy((*a)->msgid, msgid);
	    (*a)->art = art;
	    return;
	}
    }
}

int findmsgid( const char* msgid ) {
    struct msgidtree * a;
    int c;
    char * domainpart;

    /* domain part differs more than local-part, so try it first */

    domainpart = strchr( msgid, '@' );
    if ( domainpart == NULL )
	return 0;

    a = head;
    while (a) {
	c = strcmp(strchr(a->msgid, '@'), domainpart);
	if ( c == 0 )
	    c = strcmp(a->msgid, msgid);
	if ( c < 0 )
	    a = a->left;
	else if ( c > 0 )
	    a = a->right;
	else
	    return a->art;
    }
    return 0;
}

static void begone( struct msgidtree * m ) {
    if ( m ) {
	begone( m->right ) ;
	begone( m->left );
	free( (char *) m );
    }
}

void clearidtree( void ) {
    if ( head ) {
	begone( head->right ) ;
	begone( head->left );
	free( (char *) head );
    }
    head = NULL;
}


const char* rfctime(void) {
    static char date[128];
    const char * months[] = { "Jan", "Feb", "Mar", "Apr",
	"May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const char * days[] = { "Sun", "Mon", "Tue", "Wed",
	"Thu", "Fri", "Sat" };
    time_t now;
    struct tm gmt;
    struct tm local;
    int    hours, mins;
#ifndef HAVE_GMTOFF
    extern long int timezone;
#endif

    /* get local and Greenwich times */
    now = time(0);
    gmt	  = *(gmtime(&now));
    local = *(localtime(&now));

#ifdef HAVE_GMTOFF
    hours = local.tm_gmtoff/60/60;
    mins  = local.tm_gmtoff/60%60;
#else
    /* this is empirical */
    hours = -timezone/60/60+((local.tm_isdst>0)?1:0);
    mins  = -timezone/60%60;
#endif

    /* finally print the string */
    sprintf(date, "%3s, %d %3s %4d %02d:%02d:%02d %+03d%02d",
	    days[local.tm_wday], local.tm_mday, months[local.tm_mon],
	    local.tm_year+1900, local.tm_hour, local.tm_min, local.tm_sec,
	    hours, mins);

    return (date);
}

/*
 * returns 0 if string matches pattern
 */
int ngmatch(const char* pattern, const char* str) {
    return( !wildmat( str, pattern ) );
}

/*
 * copy n bytes from infile to outfile
 */
void copyfile( FILE * infile, FILE * outfile, long n ) {
    static char * buf = NULL ;
    long total;
    size_t toread, read;

    if ( n == 0 )
	return;

    if ( !buf )
	buf = critmalloc( BLOCKSIZE+1, "Allocating buffer space" );
    total = 0;
    do {
	if ( (n-total) < BLOCKSIZE )
	    toread = n - total;
	else
	    toread = BLOCKSIZE;
	read = fread( buf, sizeof(char), toread, infile );
	if ( read < toread && !feof( infile ) ) {
	    fprintf( stderr, "read error on compressed batch\n" );
            clearerr( infile );
	}
	fwrite( buf, sizeof(char), read, outfile );
	total += read;
    } while ( ( total < n ) || ( read < toread ) );
}

/**************************************************************************/

/*
 * The following routines comprise the wildmat routine. Written by
 * Rich $alz, taken vom INN 2.2.2
 */

/*  $Revision: 1.4 $
**
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  Might not be robust in face of malformed patterns; e.g., "foo[a-"
**  could cause a segmentation violation.  It is 8bit clean.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
**  Rich $alz is now <rsalz@osf.org>.
**  April, 1991:  Replaced mutually-recursive calls with in-line code
**  for the star character.
**
**  Special thanks to Lars Mathiesen <thorinn@diku.dk> for the ABORT code.
**  This can greatly speed up failing wildcard patterns.  For example:
**	pattern: -*-*-*-*-*-*-12-*-*-*-m-*-*-*
**	text 1:	 -adobe-courier-bold-o-normal--12-120-75-75-m-70-iso8859-1
**	text 2:	 -adobe-courier-bold-o-normal--12-120-75-75-X-70-iso8859-1
**  Text 1 matches with 51 calls, while text 2 fails with 54 calls.  Without
**  the ABORT code, it takes 22310 calls to fail.  Ugh.  The following
**  explanation is from Lars:
**  The precondition that must be fulfilled is that DoMatch will consume
**  at least one character in text.  This is true if *p is neither '*' nor
**  '\0'.)  The last return has ABORT instead of FALSE to avoid quadratic
**  behaviour in cases like pattern "*a*b*c*d" with text "abcxxxxx".  With
**  FALSE, each star-loop has to run to the end of the text; with ABORT
**  only the last one does.
**
**  Once the control of one instance of DoMatch enters the star-loop, that
**  instance will return either TRUE or ABORT, and any calling instance
**  will therefore return immediately after (without calling recursively
**  again).  In effect, only one star-loop is ever active.  It would be
**  possible to modify the code to maintain this context explicitly,
**  eliminating all recursive calls at the cost of some complication and
**  loss of clarity (and the ABORT stuff seems to be unclear enough by
**  itself).  I think it would be unwise to try to get this into a
**  released version unless you have a good test data base to try it out
**  on.
*/

/* YOU MUST NOT DEFINE ABORT TO 1, LEAVE AT -1 (would collide with TRUE)
 */
#define ABORT			-1

    /* What character marks an inverted character class? */
#define NEGATE_CLASS		'^'
    /* Is "*" a common pattern? */
#define OPTIMIZE_JUST_STAR
    /* Do tar(1) matching rules, which ignore a trailing slash? */
#undef MATCH_TAR_PATTERN


/*
**  Match text and p, return TRUE, FALSE, or ABORT.
*/
static int DoMatch(const char *text, const char *p)
{
    int	                last;
    int	                matched;
    int	                reverse;

    for ( ; *p; text++, p++) {
	if (*text == '\0' && *p != '*')
	    return ABORT;
	switch (*p) {
	case '\\':
	    /* Literal match with following character. */
	    p++;
	    /* FALLTHROUGH */
	default:
	    if (*text != *p)
		return FALSE;
	    continue;
	case '?':
	    /* Match anything. */
	    continue;
	case '*':
	    while (*++p == '*')
		/* Consecutive stars act just like one. */
		continue;
	    if (*p == '\0')
		/* Trailing star matches everything. */
		return TRUE;
	    while (*text)
		if ((matched = DoMatch(text++, p)))
		    return matched;
	    return ABORT;
	case '[':
	    reverse = p[1] == NEGATE_CLASS ? TRUE : FALSE;
	    if (reverse)
		/* Inverted character class. */
		p++;
	    matched = FALSE;
	    if (p[1] == ']' || p[1] == '-')
		if (*++p == *text)
		    matched = TRUE;
	    for (last = *p; *++p && *p != ']'; last = *p)
		/* This next line requires a good C compiler. */
		if (*p == '-' && p[1] != ']'
		    ? *text <= *++p && *text >= last : *text == *p)
		    matched = TRUE;
	    if (matched == reverse)
		return FALSE;
	    continue;
	}
    }

#ifdef	MATCH_TAR_PATTERN
    if (*text == '/')
	return TRUE;
#endif	/* MATCH_TAR_PATTERN */
    return *text == '\0';
}


/*
**  User-level routine.  Returns TRUE or FALSE.
**  This routine was borrowed from Rich Salz and appeared first in INN.
*/
int wildmat(const char *text, const char *p) 
{
#ifdef	OPTIMIZE_JUST_STAR
    if (p[0] == '*' && p[1] == '\0')
	return TRUE;
#endif	/* OPTIMIZE_JUST_STAR */
    return DoMatch(text, p) == TRUE; /* FIXME */
}
