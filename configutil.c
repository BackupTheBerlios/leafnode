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
#include "critmem.h"
#include "ln_log.h"
#include "config_defs.h"
#include "configparam.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
int avoidxover = 0;
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
	snprintf( s, 1023, "%s/config", libdir );
    }  else {
	snprintf( s, 1023, configfile );
    }

    if ( (f=fopen( s, "r" )) == 0 ) {
	err = errno;
        ln_log(LNLOG_ERR, "cannot open %s: %s", s, strerror(errno));
	free( param );
	free( value );
	return err;
    }
    debug = 0;
    while ( (l=getaline( f )) ) {
	const struct configparam *cp;
	debug = debugmode;
	if ( parse_line( l, param, value )
	     && (cp = find_configparam(param, strlen(param)))
	    ) {
	    switch(cp->code) {
		case CP_DEBUG:
		    debugmode = strtol( value, NULL, 10 );
		    ln_log_sys(LNLOG_DEBUG, 
			       "config: debugmode is %d", debugmode );
		    break;
		case CP_USER:
		    if ( p ) {
			p->username = strdup( value );
			if (debugmode)
			    ln_log_sys(LNLOG_DEBUG, 
				       "config: found username for %s",
				       p->name);
		    } else { 
			ln_log( LNLOG_ERR, "config: no server for username %s",
				value );
		    }
		    break;
		case CP_PASS:
		    if ( p ) {
			p->password = strdup( value );
			if ( debugmode )
			    ln_log_sys( LNLOG_DEBUG, 
					"config: found password for %s",
					p->name );
		    } else {
			ln_log( LNLOG_ERR, "config: no server for password" );
		    }
		    break;
		case CP_TIMEOUT:
		    if ( p ) {
			p->timeout = strtol( value, NULL, 10 );
			if ( debugmode )
			    ln_log_sys(LNLOG_DEBUG, 
				       "config: timeout is %d seconds",
				       p->timeout );
		    } else { 
			ln_log(LNLOG_ERR, "config: no server for timeout" );
		    }
		    break;
		case CP_LINKS:
		    if ( value && strlen( value ) ) {
			create_all_links = strtol( value, NULL, 10 );
			if ( create_all_links && debugmode )
			    ln_log_sys(LNLOG_DEBUG,
				       "config: link articles in all groups" );
		    }
		    break;
		case CP_EXPIRE:
		    expire = time(0)-(time_t)( SECONDS_PER_DAY *atol( value ));
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: expire is %s days", value);
		    break;
		case CP_FILTFIL:
		    filterfile = strdup( value );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: filterfile is %s", value );
		    break;
		case CP_HOST:
		case CP_FQDN:
		    owndn = strdup( value );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: hostname is %s", value );
		    break;
		case CP_AUTH:
		    if ( strcmp( "name", value ) == 0 )
			authentication = AM_NAME;
		    else
			authentication = AM_FILE;
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, "authentication method: %d",
				   authentication );
		    break;
		case CP_DELAY:
		    delaybody = strtol( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: delaybody is %d (default 0)",
				   delaybody );
		    break;
		case CP_AVOIDXOVER:
		    avoidxover = strtol( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: avoidxover is %d (default 0)",
				   avoidxover );
		    break;
		case CP_TOSHORT:
		    timeout_short = strtol( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: timeout_short is %d days",
				   timeout_short );
		    break;
		case CP_TOLONG:
		    timeout_long = strtol( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: timeout_long is %d days",
				   timeout_long );
		    break;
		case CP_TOACTIVE:
		    timeout_active = strtol( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: timeout_active is %d days",
				   timeout_active );
		    break;
		case CP_GROUPEXP: 
		{
		    char * m = param;
		    
		    while (!(isspace((unsigned char)*m))) {
			*m = tolower((unsigned char)*m);
			m++;
		    }
		    while (isspace((unsigned char)*m)) m++;
		    if ( m && *m ) {
			i = time(NULL)-(time_t)( SECONDS_PER_DAY *atol(value));
			ent = (struct expire_entry *)
			    malloc( sizeof(struct expire_entry) );
			ent->group = strdup(m);
			ent->xtime = i;
			ent->next = prev;
			prev = ent;
			if ( debugmode )
			    ln_log_sys(LNLOG_DEBUG,
				       "config: groupexpire for %s is %s days",
				       m, value);
		    }
		}
		break;
		case CP_MAXAGE:
		case CP_MAXOLD:
		case CP_MAXXP:
		case CP_MAXGR:
		case CP_MAXLN:
		case CP_MINLN:
		case CP_MAXBYT:
		    ln_log(LNLOG_INFO, 
			   "%s is obsolete: use filterfile instead",
			   param );
		    break;
		case CP_MAXFETCH:
		    artlimit = strtoul( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, "config: maxfetch is %lu", 
				   artlimit);
		    break;
		case CP_PORT:
		    if ( p ) {
			p->port = strtol( value, NULL, 10 );
			if ( debugmode )
			    ln_log_sys(LNLOG_DEBUG, "config: nntpport is %d",
				       p->port );
		    } else
			ln_log(LNLOG_ERR, "config: no server for nntpport %s",
			       value );
		    break;
		case CP_NODESC:
		    if ( p ) {
			p->descriptions = FALSE;
			if ( debugmode )
			    ln_log_sys(LNLOG_DEBUG, 
				       "config: no LIST NEWSGROUPS for %s",
				       p->name );
		    } else
			ln_log(LNLOG_ERR, "config: no server for nodesc = %s",
			       value );
		    break;
		case CP_INITIAL:
		    initiallimit = strtoul( value, NULL, 10 );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, "config: initialfetch is %lu",
				   initiallimit);
		    break;
		case CP_PSEUDO:
		    pseudofile = strdup( value );
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, 
				   "config: read pseudoarticle from %s",
				   pseudofile );
		    break;
		case CP_SERVER:
		case CP_SUPPL:
		    if ( debugmode )
			ln_log_sys(LNLOG_DEBUG, "config: server is %s", value );
		    p = (struct serverlist *)critmalloc( 
			sizeof(struct serverlist),
			"allocating space for server name");
		    p->name = strdup( value );
		    p->descriptions = TRUE;
		    p->next = NULL;
		    p->timeout = 30;	/* default 30 seconds */
		    p->port = 0;
		    p->username = NULL;
		    p->password = NULL;
		    p->active = TRUE;
		    if ( !servers )
			servers = p;
		    else
			q->next = p;
		    q = p;
		    break;
	    } /* switch */
	}
	debug = 0;
    }
    debug = debugmode;

    expire_base = ent;
    fclose(f);
    if ( !servers ) {
	ln_log(LNLOG_ERR, "no server declaration in config file");
	free( param );
	free( value );
	return EDESTADDRREQ;
    }
    if ( !expire )
	ln_log(LNLOG_ERR, "no expire declaration in config file");
    
    if ( debugmode > 1 ) {
	getrlimit( RLIMIT_CORE, &corelimit );	/* initialize struct */
        corelimit.rlim_cur = COREFILESIZE;
        if ( setrlimit( RLIMIT_CORE, &corelimit ) < 0 )
            ln_log_sys(LNLOG_DEBUG, "Changing core file size failed: %s",
		       strerror(errno));
        corelimit.rlim_cur = 0;
        if ( getrlimit( RLIMIT_CORE, &corelimit ) < 0 )
	    ln_log_sys(LNLOG_DEBUG, "Getting core file size failed: %s",
		       strerror(errno));
	else
	    ln_log_sys(LNLOG_DEBUG, "Core file size: %lu", (unsigned long)corelimit.rlim_cur );
    }

    free( param );
    free( value );
    return 0;
}
