/*
libutil -- read filter file and do filtering of messages

Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
Copyright 1998.

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

struct filterlist * filter = NULL;

/*
 * find "needle" in "haystack" only if "needle" is at the beginning of a line
 * returns a pointer to the first char after "needle" which should be a
 * whitespace
 */
static char * findinheaders( char * needle, char * haystack ) {
    static char *p; 
    char *q;

    p = haystack;
    while ( p && *p ) {
	if ( strncasecmp( needle, p, strlen( needle ) ) == 0 ) {
	    p += strlen( needle );
	    if ( isspace((unsigned char)*p) )
		return p;
	}
	else {
	    q = p;
	    p = strchr( q, '\n' );
	    if ( p && *p )
		p++;
	}
    }
    return NULL;
}

/*
 * calculate date in seconds since Jan 1st 1970 from a Date: header
 */
static int age( const char * date ) {
    char monthname[4];
    int month;
    int year;
    int day;
    const char * d;
    time_t tmp;
    struct tm time_struct;

    if ( !date )
	return 1000; /* large number: OLD */
    d = date;
    if ( strncmp( date, "Date:", 5) == 0 )
	d += 5;
    while( isspace((unsigned char)*d) )
	d++;

    if ( isalpha((unsigned char)*d) ) {
	while ( !isspace((unsigned char)*d) )	/* skip "Mon" or "Tuesday," */
	    d++;
    }

    /* parsing with sscanf leads to crashes */
    day = strtol( d, NULL, 10 );
    while ( isdigit((unsigned char)*d) || isspace((unsigned char)*d) )
	d++;
    if ( !isalpha((unsigned char)*d) ) {
	syslog( LOG_INFO, "Unable to parse %s", date );
	return 1003;
    }
    monthname[0] = *d++;
    monthname[1] = *d++;
    monthname[2] = *d++;
    monthname[3] = '\0';
    if ( strlen(monthname) != 3 ) {
	syslog( LOG_INFO, "Unable to parse month in %s", date );
	return 1004;
    }
    while ( isalpha((unsigned char)*d) )
	d++;
    while ( isspace((unsigned char)*d) )
	d++;
    year = strtol( d, NULL, 10 );

    if ( ( year < 1970 ) && ( year > 99 ) ) {
	syslog( LOG_INFO, "Unable to parse year in %s", date );
	return 1005;
    } else if ( !(day > 0 && day < 32) ) {
	syslog( LOG_INFO, "Unable to parse day in %s", date );
	return 1006;
    } else {
	if ( !strcasecmp( monthname, "jan" ) )
	    month = 0;
	else if ( !strcasecmp( monthname, "feb" ) )
	    month = 1;
	else if ( !strcasecmp( monthname, "mar" ) )
	    month = 2;
	else if ( !strcasecmp( monthname, "apr" ) )
	    month = 3;
	else if ( !strcasecmp( monthname, "may" ) )
	    month = 4;
	else if ( !strcasecmp( monthname, "jun" ) )
	    month = 5;
	else if ( !strcasecmp( monthname, "jul" ) )
	    month = 6;
	else if ( !strcasecmp( monthname, "aug" ) )
	    month = 7;
	else if ( !strcasecmp( monthname, "sep" ) )
	    month = 8;
	else if ( !strcasecmp( monthname, "oct" ) )
	    month = 9;
	else if ( !strcasecmp( monthname, "nov" ) )
	    month = 10;
	else if ( !strcasecmp( monthname, "dec" ) )
	    month = 11;
	else {
	    syslog( LOG_INFO, "Unable to parse %s", date );
	    return 1001;
	}
	if ( year < 70 )        /* years 2000-2069 in two-digit form */
	    year += 100;
	else if ( year > 1970 ) /* years > 1970 in four-digit form */
	    year -= 1900;

	memset( &time_struct, 0, sizeof(time_struct) );
        time_struct.tm_sec = 0;
	time_struct.tm_min = 0;
	time_struct.tm_hour = 0;
	time_struct.tm_mday = day;
	time_struct.tm_mon = month;
	time_struct.tm_year = year;
	time_struct.tm_isdst = 0;

	tmp = mktime( &time_struct );

	if ( tmp == -1 )
	    return 1002;
	
	return( ( time(NULL) - tmp ) / SECONDS_PER_DAY );
    }
}

/*
 * create a new filterentry for a list
 */
static struct filterlist * newfilter( void ) {
    struct filterlist *fl;
    struct filterentry *fe;

    fe = (struct filterentry *)critmalloc( sizeof(struct filterentry),
					  "Allocating filterentry space" );
    fe->newsgroup = NULL;
    fe->cleartext = NULL;
    fe->expr = NULL;
    fe->limit = -1;
    fe->action = NULL;

    fl = (struct filterlist *)critmalloc( sizeof(struct filterlist),
					  "Allocating filterlist space" );
    fl->next = NULL;
    fl->entry = fe;
 
    return fl;
}

/*
 * read filters into memory. Filters are just plain regexp's
 * return TRUE for success, FALSE for failure
 *
 * in addition, we initialize four standard regular expressions
 */
int readfilter( char *filterfile ) {
    FILE * ff;
    char * l;
    char * ng = NULL;
    char * param, * value;
    const char *regex_errmsg;
    int regex_errpos;
    struct filterlist * f, * oldf = NULL;

    if ( filterfile == NULL || !strlen(filterfile) )
	return FALSE;

    param = critmalloc( TOKENSIZE, "allocating space for parsing" );
    value = critmalloc( TOKENSIZE, "allocating space for parsing" );
    filter = NULL;
    f = NULL;

    ff = fopen( filterfile, "r" );
    if ( !ff ) {
	syslog( LOG_WARNING, "Unable to open filterfile %s: %m", filterfile );
	if ( verbose )
	    printf( "Unable to open filterfile %s\n", filterfile );
	return FALSE;
    }
    debug = 0;
    while ( ( l = getaline( ff ) ) != NULL ) {
	if ( parse_line( l, param, value ) ) {
	    if ( strcasecmp( "newsgroup", param ) == 0 ||
		 strcasecmp( "newsgroups", param ) == 0 ) {
		ng = strdup( value );
		f = newfilter();
		(f->entry)->newsgroup = ng;
		if ( !filter )
		    filter = f;
		else
		    oldf->next = f;
		oldf = f;
	    }
	    else if ( strcasecmp( "pattern", param ) == 0 ) {
		if ( !ng ) {
		    syslog( LOG_NOTICE,
			    "No newsgroup for expression %s found", value );
		    if ( verbose )
			printf( "No newsgroup for expression %s found", value );
		    continue;
		}
		if ( !f ) {
		    f = newfilter();
		    (f->entry)->newsgroup = ng;
		    if ( !filter )
			filter = f;
		    else
			oldf->next = f;
		    oldf = f;
		}
#ifdef NEW_PCRE_COMPILE
		if ( ( (f->entry)->expr = pcre_compile( value, PCRE_MULTILINE ,
		       &regex_errmsg, &regex_errpos, NULL ) ) == NULL ) {
#else
		if ( ( (f->entry)->expr = pcre_compile( value, PCRE_MULTILINE ,
		       &regex_errmsg, &regex_errpos ) ) == NULL ) {
#endif
		    syslog( LOG_NOTICE, "Invalid filter pattern %s: %s",
			    value, regex_errmsg );
		    if ( verbose )
			printf( "Invalid filter pattern %s: %s",
				value, regex_errmsg );
		    free(f->entry);
		    f->entry = NULL;
		}
		else {
		    (f->entry)->cleartext = strdup( value );
		    (f->entry)->limit	  = -1 ;
		}
	    }
	    else if ( ( strcasecmp( "maxage", param ) == 0 ) ||
		      ( strcasecmp( "minlines", param ) == 0 ) ||
		      ( strcasecmp( "maxlines", param ) == 0 ) ||
		      ( strcasecmp( "maxbytes", param ) == 0 ) ||
		      ( strcasecmp( "maxcrosspost", param ) == 0 ) ) {
	        if ( !ng ) {
		    syslog( LOG_NOTICE,
			    "No newsgroup for expression %s found", value );
		    if ( verbose )
			printf( "No newsgroup for expression %s found", value );
		    continue;
		}
		if ( !f ) {
		    f = newfilter();
		    (f->entry)->newsgroup = ng;
		    if ( !filter )
		        filter = f;
		    else
			oldf->next = f;
		    oldf = f;
		}
		(f->entry)->cleartext = strdup( param );
		(f->entry)->limit = (int)strtol( value, NULL, 10 );
	    }
	    else if ( strcasecmp( "action", param ) == 0 ) {
		if ( !f || !f->entry || !(f->entry)->cleartext ) {
		    syslog( LOG_NOTICE, "No pattern found for action %s",
			    value );
		    if ( (f->entry)->expr )
			free( (f->entry)->expr );
		    if ( f->entry )
			free(f->entry);
		    if ( f )
			free(f);
		    continue ;
		}
		else {
		    (f->entry)->action = strdup( value );
/*
		    if ( debugmode ) {
			syslog( LOG_DEBUG, "filtering in %s: %s -> %s",
				(f->entry)->newsgroup,
				(f->entry)->cleartext,
				(f->entry)->action );
		    }
*/
		}
	    }
	}
    }
    debug = debugmode;
    fclose( ff );
    if ( filter == NULL ) {
	syslog( LOG_WARNING, "filterfile did not contain any valid patterns" );
	return FALSE;
    }
    else
	return TRUE;
}

/*
 * create a new filterlist with filters matching the actual newsgroup
 */
struct filterlist * selectfilter ( char * groupname ) {
    struct filterlist *master;
    struct filterlist *f, *fold, *froot;

    froot = NULL;
    fold  = NULL;
    master = filter;
    while ( master ) {
	if ( ngmatch( (master->entry)->newsgroup, groupname ) == 0 ) {
	    f = (struct filterlist *)critmalloc( sizeof(struct filterlist),
					 "Allocating groupfilter space" );
	    f->entry = master->entry;
	    f->next = NULL;
	    if ( froot == NULL )
		froot = f;
	    else
		fold->next = f;
	    fold = f;
	    if ( debugmode ) {
		syslog( LOG_DEBUG, "filtering in %s: %s -> %s",
			groupname, (f->entry)->cleartext, (f->entry)->action );
	    }
	}
	master = master->next;
    }
    return froot;
}

/*
 * read and filter headers.
 * Return true if article should be killed, false if not
 */
int killfilter( struct filterlist *f, char *hdr ) {
    int match, score ;
    struct filterentry * g;
    char * p;

    if ( !f )
	return FALSE;

    score = 0;
    match = -1 ;
    while ( f ) {
	g = f->entry;
	if ( ( g->limit == -1 ) && ( g->expr ) ) {
#ifdef NEW_PCRE_EXEC
	    match = pcre_exec( g->expr, NULL, hdr, (int)strlen(hdr), 0, 0, NULL, 0 );
#else
	    match = pcre_exec( g->expr, NULL, hdr, (int)strlen(hdr), 0, NULL, 0 );
#endif
	}
	else if ( strcasecmp( g->cleartext, "maxage" ) == 0 ) {
	    p = findinheaders( "Date:", hdr );
	    while ( p && *p && isspace((unsigned char)*p) )
		p++;
	    if ( age(p) > g->limit )
		match = 0;	/* limit has been hit */
	    else
		match = PCRE_ERROR_NOMATCH;  /* don't match by default */
	}
	else if ( strcasecmp( g->cleartext, "maxlines" ) == 0 ) {
	    p = findinheaders( "Lines:", hdr );
	    if (p) {
		if ( strtol( p, NULL, 10 ) > g->limit )
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	}
	else if ( strcasecmp( g->cleartext, "minlines" ) == 0 ) {
	    p = findinheaders( "Lines:", hdr );
	    if (p) {
		if ( strtol( p, NULL, 10 ) < g->limit )
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	}
	else if ( strcasecmp( g->cleartext, "maxbytes" ) == 0 ) {
	    p = findinheaders( "Bytes:", hdr );
	    if (p) {
		if ( strtol( p, NULL, 10 ) > g->limit )
		    match = 0;
		else
		    match = PCRE_ERROR_NOMATCH;
	    }
	}
	else if ( strcasecmp( g->cleartext, "maxcrosspost" ) == 0 ) {
	    p = findinheaders( "Newsgroups:", hdr );
	    match = 1;
	    while ( *p && *p != '\n' ) {
		if ( *p++ == ',' )
		    match++;
	    }
	    if ( match > g->limit )
		match = 0;
	    else
		match = PCRE_ERROR_NOMATCH;
	}
	if ( match == 0 ) {
	    /* article matched pattern/limit: what now? */
	    if ( strcasecmp( g->action, "select" ) == 0 ) {
		return FALSE;
	    }
	    else if ( strcasecmp( g->action, "kill" ) == 0 ) {
		return TRUE;
	    }
	    else {
		score += strtol( g->action, NULL, 10 );
	    }
	}
	f = f->next ;
    }
    if ( score < 0 )
	return TRUE;
    else
	return FALSE;
}

/*
 * free filterlist but not filterentries
 */
void freefilter( struct filterlist *f ) {
    struct filterlist *g;

    while ( f ) {
	g = f->next ;
	free( f );
	f = g;
    }
}
