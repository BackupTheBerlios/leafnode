/*
libutil -- read config file

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
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/resource.h>

#define COREFILESIZE 1024*1024*4
#define TOKENSIZE 80

/*
 * misc. global variables, documented in leafnode.h
 */
time_t expire = 0;
struct expire_entry * expire_base;
unsigned long artlimit = 0;
unsigned long initiallimit = 0;
int create_all_links = 0;
int delaybody = 0;
int timeout_long = 7;
int timeout_short = 2;
int timeout_active = 90;
int authentication = 0;	/* use authentication for NNTP access:
			   possible values defined in leafnode.h */
int filtermode = FM_XOVER | FM_HEAD ;
			/* filter xover headers or heads or both (default) */
char * filterfile = NULL ;
char * pseudofile = NULL ;	/* filename containing pseudoarticle body */
char * owndn = NULL;	/* own domain name, if you can't set a sensible one */
struct serverlist *servers = NULL;	/* list of servers to fetch news from */

/* parse a line, destructively */
int parse_line ( char *l, char * param, char * value ) {
    char *p, *q;

    p = l;
    /* skip comments */
    q = strchr( p, '#' );
    if ( q )
       *q = '\0';
    if ( *p == 0 )
        return 0;
    if (( q = strchr( p, '=' ) ) == NULL )
	return 0;
    else
	*q++ = '\0';
    /* skipping leading blanks or tabs */
    while ( isspace((unsigned char)*p) )
	p++;
    while ( isspace((unsigned char)*q) )
	q++;
    strncpy( param, p, TOKENSIZE );
    strncpy( value, q, TOKENSIZE );
    /* now param contains the stuff before '=' and value the stuff behind it */
    if ( !(*param) || !(*value) )
	return 0;
    /* skipping trailing blanks or tabs */
    p = param + strlen(param) - 1;
    q = value + strlen(value) - 1;
    while ( isspace((unsigned char)*p) && ( p > param ) ) {
	*p = '\0';
	p--;
    }
    while ( isspace((unsigned char)*q) && ( q > value ) ) {
	*q = '\0';
	q--;
    }
    return 1;
}

/*
05/25/97 - T. Sweeney - Modified to read user name and password for AUTHINFO.
                        Security questionable as password is stored in
                        plaintext in insecure file.
*/

/*
 * read configuration. Return 0 upon success, values > 0 (taken from
 * errno.h) otherwise
 */
int readconfig( char * configfile ) {
    struct serverlist *p = NULL, *q = NULL;
    struct rlimit corelimit ;
    struct expire_entry *ent = NULL, *prev = NULL;
    FILE * f;
    char * l;
    char * param, * value ;
    int i, err;

    artlimit = 0;
    param = critmalloc( TOKENSIZE, "allocating space for parsing" );
    value = critmalloc( TOKENSIZE, "allocating space for parsing" );

    if ( configfile == NULL ) {
	sprintf( s, "%s/config", libdir );
    }
    else {
	/* use snprintf() because otherwise everybody can cause
	   buffer overflows */
	snprintf( s, 1023, configfile );
    }
    if ( (f=fopen( s, "r" )) == 0 ) {
        syslog( LOG_ERR, "cannot open %s", s );
	err = errno;
	free( param );
	free( value );
	return err;
    }
    debug = 0;
    while ( (l=getaline( f )) ) {
	debug = debugmode;
	if ( parse_line( l, param, value ) ) {
	    if ( strcmp ( "debugmode", param ) == 0 ) {
		debugmode = strtol( value, NULL, 10 );
		syslog( LOG_DEBUG, "config: debugmode is %d", debugmode );
	    }
            else if ( strcmp ( "username", param ) == 0 ) {
	        if ( p ) {
		    p->username = strdup( value );
		    if ( debugmode )
	        	syslog( LOG_DEBUG, "config: found username for %s",
				p->name );
		}
		else
		    syslog( LOG_ERR, "config: no server for username %s",
			    value );
	    }
	    else if ( strcmp ( "password", param ) == 0 ) {
		if ( p ) {
		    p->password = strdup( value );
		    if ( debugmode )
			syslog( LOG_DEBUG, "config: found password for %s",
				p->name );
		}
		else
		    syslog( LOG_ERR, "config: no server for password" );
	    }
	    else if ( strcmp( "timeout", param ) == 0 ) {
		if ( p ) {
		    p->timeout = strtol( value, NULL, 10 );
		    if ( debugmode )
			syslog( LOG_DEBUG, "config: timeout is %d seconds",
				p->timeout );
		}
		else
		    syslog( LOG_ERR, "config: no server for timeout" );
	    }
	    else if ( strcmp ( "create_all_links", param ) == 0 ) {
		if ( value && strlen( value ) ) {
		    create_all_links = strtol( value, NULL, 10 );
		    if ( create_all_links && debugmode )
			syslog( LOG_DEBUG,
				"config: link articles in all groups" );
		}
	    }
	    else if ( strcmp ( "expire", param ) == 0 ) {
	        expire = time(NULL)-(time_t)( SECONDS_PER_DAY *atol( value ));
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: expire is %s days", value);
	    }
	    else if ( strcmp ( "filterfile", param ) == 0 ) {
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: filterfile is %s", value );
		filterfile = strdup( value );
	    }
	    else if ( ( strcmp ( "hostname", param ) == 0 ) ||
		      ( strcmp ( "fqdn", param ) == 0 ) ) {
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: hostname is %s", value );
		owndn = strdup( value );
	    }
	    else if ( strcmp( "authenticate", param ) == 0 ) {
		if ( strcmp( "name", value ) == 0 )
		    authentication = AM_NAME;
		else
		    authentication = AM_FILE;
		if ( debugmode )
		    syslog( LOG_DEBUG, "authentication method: %d",
			    authentication );
	    }
	    else if ( strcmp( "delaybody", param ) == 0 ) {
		delaybody = strtol( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: delaybody is %d (default 0)",
			    delaybody );
	    }
	    else if ( strcmp( "timeout_short", param ) == 0 ) {
		timeout_short = strtol( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: timeout_short is %d days",
			    timeout_short );
	    }
	    else if ( strcmp( "timeout_long", param ) == 0 ) {
		timeout_long = strtol( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: timeout_long is %d days",
			    timeout_long );
	    }
	    else if ( strcmp( "timeout_active", param ) == 0 ) {
		timeout_active = strtol( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: timeout_active is %d days",
			    timeout_active );
	    }
	    else if ( strncmp( "groupexpire", param, 11 ) == 0 ) {
		char * m;
		m = param;
		while ( !(isspace((unsigned char)*m)) ) {
		    *m = tolower((unsigned char)*m);
		    m++;
		}
		while ( isspace((unsigned char)*m) )
		    m++;
		if ( m && *m ) {
		    i = time(NULL)-(time_t)( SECONDS_PER_DAY *atol(value));
                    ent = (struct expire_entry *)
                           malloc( sizeof(struct expire_entry) );
                    ent->group = strdup(m);
                    ent->xtime = i;
                    ent->next = prev;
                    prev = ent;
		    if ( debugmode )
		        syslog( LOG_DEBUG,
				"config: groupexpire for %s is %s days",
				m, value);
		}
            }
	    else if ( ( strcmp( "maxage", param ) == 0 ) ||
		      ( strcmp( "maxold", param ) == 0 ) ||
		      /* maxold was for compatibility with leafnode+ */
		      ( strcmp( "maxcrosspost", param ) == 0 ) ||
		      ( strcmp( "maxgroups", param ) == 0 ) ||
		      /* maxgroups was for compatibility with leafnode+ */
		      ( strcmp( "maxlines", param ) == 0 ) ||
		      ( strcmp( "minlines", param ) == 0 ) ||
		      ( strcmp( "maxbytes", param ) == 0 ) ) {
		if ( verbose )
		    fprintf( stderr, "%s is obsolete: use filterfile instead\n",
			     param );
		syslog( LOG_INFO, "%s is obsolete: use filterfile instead",
			param );
	    }
	    else if ( strcmp( "maxfetch", param ) == 0 ) {
	        artlimit = strtoul( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: maxfetch is %lu", artlimit);
	    }
	    else if ( strcmp( "port", param ) == 0 ) {
		if ( p ) {
		    p->port = strtol( value, NULL, 10 );
		    if ( debugmode )
			syslog( LOG_DEBUG, "config: nntpport is %d",
			p->port );
		}
		else
		    syslog( LOG_ERR, "config: no server for nntpport %s",
			    value );
	    }
	    else if ( strcmp( "nodesc", param ) == 0 ) {
		if ( p ) {
		    p->descriptions = FALSE;
		    if ( debugmode )
			syslog( LOG_DEBUG, "config: no LIST NEWSGROUPS for %s",
			p->name );
		}
		else
		    syslog( LOG_ERR, "config: no server for nodesc = %s",
			    value );
	    }
	    else if ( strcmp( "initialfetch", param ) == 0 ) {
	        initiallimit = strtoul( value, NULL, 10 );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: initialfetch is %lu",
			    initiallimit);
	    }
	    else if ( strcmp( "pseudoarticle", param ) == 0 ) {
		pseudofile = strdup( value );
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: read pseudoarticle from %s",
			    pseudofile );
	    }
	    else if (( strcmp ( "server", param ) == 0 ) ||
		    ( strcmp ( "supplement", param ) == 0 )) {
		if ( debugmode )
		    syslog( LOG_DEBUG, "config: server is %s", value );
		p = (struct serverlist *)critmalloc( sizeof(struct serverlist),
		     "allocating space for server name");
		p->name = strdup( value );
		p->descriptions = TRUE;
		p->next = NULL;
		p->timeout = 30;	/* default 30 seconds */
		p->port = 0;
		p->username = NULL;
		p->password = NULL;
		p->active = TRUE;
		if ( servers == NULL )
		    servers = p;
		else
		    q->next = p;
		q = p;
	    }
	}
	debug = 0;
    }
    debug = debugmode;

    expire_base = ent;
    fclose(f);
    if ( servers == NULL ) {
	syslog( LOG_ERR, "no server declaration in config file");
	free( param );
	free( value );
	return EDESTADDRREQ;
    }
    if ( !expire )
	syslog( LOG_ERR, "no expire declaration in config file");
    
    if ( debugmode > 1 ) {
	getrlimit( RLIMIT_CORE, &corelimit );	/* initialize struct */
        corelimit.rlim_cur = COREFILESIZE;
        if ( setrlimit( RLIMIT_CORE, &corelimit ) < 0 )
            syslog( LOG_DEBUG, "Changing core file size failed: %m" );
        corelimit.rlim_cur = 0;
        if ( getrlimit( RLIMIT_CORE, &corelimit ) < 0 )
	    syslog( LOG_DEBUG, "Getting core file size failed: %m" );
	else
	    syslog( LOG_DEBUG, "Core file size: %d", corelimit.rlim_cur );
    }

    free( param );
    free( value );
    return 0;
}
