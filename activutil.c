/*
 libutil -- deal with active file

 Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 Copyright 2000.
 Reused some old code written by Arnt Gulbrandsen <agulbra@troll.no>,
 copyright 1995, modified by (amongst others) Cornelius Krasel
 <krasel@wpxx02.toxi.uni-wuerzburg.de>, Randolf Skerka
 <Randolf.Skerka@gmx.de>, Kent Robotti <robotti@erols.com> and
 Markus Enzenberger <enz@cip.physik.uni-muenchen.de>. Copyright
 for the modifications 1997-1999.

 See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int activesize;
struct newsgroup * active = NULL;

struct nglist {
    struct newsgroup * entry;
    struct nglist * next;
} ;

static int _compactive( const void *a, const void *b ) {
    struct newsgroup *la = (struct newsgroup *)a;
    struct newsgroup *lb = (struct newsgroup *)b;

    return strcasecmp(la->name, lb->name);
}

struct nglist * newgroup;

/*
 * insert a group into a list of groups. If a group is already present,
 * it will not be affected.
 */
void insertgroup( const char *name, long unsigned first,
    long unsigned last, int age, char *desc ) {
    struct nglist * l;
    static struct nglist * lold;
    struct newsgroup * g;

    g = findgroup( name );
    if ( g )
	return;

    g = (struct newsgroup *)critmalloc( sizeof(struct newsgroup),
	"Allocating space for new group");
    g->name = strdup( name );
    g->first = first;
    g->last  = last;
    g->count = 0;
    g->age = age;
    if ( desc == NULL )
	g->desc = NULL;
    else
	g->desc = strdup( desc );
    l = (struct nglist *)critmalloc(sizeof(struct nglist),
	"Allocating space for newsgroup list");
    l->entry = g;
    l->next = NULL;
    if ( newgroup == NULL )
	newgroup = l;
    else
	lold->next = l;
    lold = l;
}

/*
 * change description of newsgroup
 */
void changegroupdesc( const char * groupname, char * description ) {
    struct newsgroup * ng;

    if ( groupname && description ) {
	ng = findgroup( groupname );
	if ( ng )
	    ng->desc = strdup( description );
    }
}

/*
 * merge nglist with active group, then free it
 */
void mergegroups( void ) {
    struct nglist * l, *la;
    int count = 0;
    
    l = newgroup;
    while ( l ) {
	count++;
	l = l->next;
    }

    active = (struct newsgroup *)critrealloc( (char *)active,
    		(1+count+activesize) * sizeof(struct newsgroup),
		"reallocating active" );
    
    l = newgroup;
    count = activesize;
    while ( l ) {
	la = l;
	active[count].name  = (l->entry)->name ;
	active[count].first = (l->entry)->first;
	active[count].last  = (l->entry)->last ;
	active[count].age   = (l->entry)->age  ;
	active[count].desc  = (l->entry)->desc ;
	l = l->next;
	count ++;
	free( la );	/* clean up */
    }
    newgroup = NULL;
    active[count].name = NULL;

    activesize = count;
    qsort( active, activesize, sizeof(struct newsgroup), &_compactive );
}

/*
 * find a group by name
 */
static unsigned long helpfindgroup( const char *name, long low, long high ) {
    int result;
    unsigned long new;

    if ( low > high )
	return -1 ;
    new = (high-low)/2+low;
    result = strcasecmp( name, active[new].name );
    if ( result == 0 )
	return new;
    else if ( result < 0 )
	return helpfindgroup( name, low, new-1 );
    else
	return helpfindgroup( name, new+1, high );
}

/*
 * find a newsgroup in the active file
 */
struct newsgroup * findgroup(const char *name) {
    long i;

    i = helpfindgroup( name, 0, activesize-1 );
    if ( i < 0 )
	return NULL;
    else
	return (&active[i]);
}

/*
 * write active file
 */
void writeactive( void ) {
    FILE * a;
    struct newsgroup * g;
    char *c;
    char l[PATH_MAX];
    int count, err;

    c = critmalloc( PATH_MAX, "Allocating buffer" );

    strcpy(s, spooldir);
    strcat(s, "/leaf.node/groupinfo.new");
    a = fopen( s, "w" );
    if ( !a ) {
	syslog( LOG_ERR, "cannot open new groupinfo file: %m" );
	return;
    }

    /* count members in array and sort it */
    g = active;
    count = 0;
    while ( g->name ) {
	count++;
	g++;
    }
    qsort( active, count, sizeof(struct newsgroup), &_compactive );

    g = active;
    err = 0;
    while ( ( err != EOF ) && g->name ) {
	if ( strlen( g->name ) ) {
	    snprintf( l, PATH_MAX,
		      "%s %lu %lu %lu %s\n", g->name, g->last, g->first,
		      (unsigned long) g->age,
		      g->desc && *(g->desc) ? g->desc : "-x-" );
	    err = fputs( l, a );
	}
	g++;
    }
    fclose( a );
    if ( err == EOF ) {
	syslog( LOG_ERR, "failed writing new groupinfo file (disk full?)" );
	unlink(s);
	return;
    }
    strcpy(c, spooldir);
    strcat(c, "/leaf.node/groupinfo");
    rename( s, c );
    free(c);
}

/*
 * free active list. Written by Lloyd Zusman
 */
static void freeactive( void ) {
    struct newsgroup * g;

    if ( active == NULL )
	return;

    g = active;
    while ( g->name ) {
	free( g->name );
	if ( g->desc )
	    free( g->desc );
	g++;
    }

    free( active );
}

/*
 * read active file into memory
 */
void readactive( void ) {
    char *buf;
    char *p, *q, *r;
    long bufsize;
    int n;
    struct stat st;
    FILE *f;
    struct newsgroup * g;

    if ( active ) {
	freeactive();
	active = NULL;
    }

    strcpy(s, spooldir);
    strcat(s, "/leaf.node/groupinfo");
    if ( stat( s, &st ) ) {
    	syslog( LOG_ERR, "can't stat %s: %m", s );
	return;
    } else if ( !S_ISREG( st.st_mode ) ) {
    	syslog( LOG_ERR, "%s not a regular file", s );
	return;
    }
    buf = critmalloc( st.st_size+2, "Reading group info" );
    if (( f = fopen( s, "r" )) != NULL ) {
    	bufsize = fread( buf, 1, st.st_size, f );
	if ( bufsize < st.st_size ) {
	    syslog( LOG_ERR,
	    	    "Groupinfo file truncated while reading: %d < %d .",
		    bufsize, (int) st.st_size );
	}
	fclose( f );
    }
    else {
     	syslog( LOG_ERR, "unable to open %s: %m", s );
	return;
    }

    bufsize = ( bufsize > st.st_size ) ? st.st_size : bufsize ;
    			/* to read truncated groupinfo files correctly */
    buf[bufsize++] = '\n';
    buf[bufsize++] = '\0';	/* 0-terminate string */
    buf = critrealloc( buf, bufsize, "Reallocating active file size" );

    /* delete spurious 0-bytes except not the last one */
    while (( p = memchr( buf, '\0', bufsize-1 ) ) != NULL )
	*p = ' ';	/* \n might be better, but produces more errors */

    /* count lines = newsgroups */
    activesize = 0;
    p = buf;
    while ( p && *p && (( q = memchr( p, '\n', (buf-p+bufsize) )) != NULL ) ) {
	activesize++;
	p = q+1;
    }

    active = (struct newsgroup *)critmalloc( (1+activesize)*
		 sizeof(struct newsgroup), "allocating active" );
    g = active;

    p = buf;
    while ( p && *p ) {
	q = strchr( p, '\n' );
	if ( q ) {
	    *q = '\0';
	    if ( strlen( p ) == 0 ) {
		p = q+1;
		continue ;		/* skip blank lines */
	    }
	}
	r = strchr( p, ' ' );
	if ( !q || !r ) {
	    if ( !q && r )
		*r = '\0';
	    else if ( q && !r )
		*q = '\0';
	    else if ( strlen(p) > 30 ) {
		q = p+30;
		*q = '\0';
	    }
	    syslog( LOG_ERR,
		    "Groupinfo file possibly truncated or damaged: >%s<",
		    p );
	    break;
	}
	*r++ = '\0';
	*q++ = '\0';

	g->name = strdup(p);
	if ( sscanf(r, "%lu %lu %lu",
		    &g->last, &g->first, &g->age) != 3 ) {
	    syslog( LOG_ERR,
		    "Groupinfo file possibly truncated or damaged: %s", p );
	    break;
	}
	if ( g->first == 0 )
	    g->first = 1;		/* pseudoarticle */
	if ( g->last == 0 )
	    g->last = 1;
	g->count = 0;
	p = r;
	for ( n = 0; n < 3; n++ ) {	/* Skip the numbers */
	    p = strchr( r, ' ' );
	    r = p+1;
	}
	if ( strcmp( r, "-x-" ) == 0 )
	    g->desc = NULL;
	else
	    g->desc = strdup(r);

        p = q;		/* next record */
	g++;
    }
    free( buf );
    /* last record, to mark end of array */
    g->name = NULL;
    g->first = 0;
    g->last = 0;
    g->age = 0;
    g->desc = NULL;

    /* count member in the array - maybe there were some empty lines */
    g = active;
    activesize = 0;
    while ( g->name ) {
	g++;
	activesize++;
    }

    /* sort the thing, just to be sure */
/*
    qsort( active, activesize, sizeof(struct newsgroup), &_compactive );
*/
}

/*
 * fake active file if it cannot be retrieved from the server
 */
void fakeactive( void ) {
    DIR * d;
    struct dirent * de;
    DIR * ng;
    struct dirent * nga;
    long unsigned int i;
    long unsigned first, last;
    char * p;

    strcpy( s, spooldir );
    strcat( s, "/interesting.groups" );
    d = opendir( s );
    if ( !d ) {
	syslog( LOG_ERR, "cannot open directory %s: %m", s );
	return;
    }

    while ( (de = readdir(d)) ) {
	if ( isalnum((unsigned char)*(de->d_name) ) &&
	     chdirgroup(de->d_name, FALSE) ) {
	    /* get first and last article from the directory. This is
	     * the most secure way to get to that information since the
	     * .overview files may be corrupt as well
	     * If the group doesn't exist, just ignore the active entry.
	     */

	    first = INT_MAX;
	    last = 0;

	    ng = opendir( "." );
	    while ( ( nga = readdir( ng ) ) != NULL ) {
		if ( isdigit ((unsigned char) *(nga->d_name ) ) ) {
		    p = NULL;
		    i = strtoul( nga->d_name, &p, 10 );
		    if ( *p == '\0' ) {
			if ( i < first )
		    	    first = i;
			if ( i > last )
		    	    last = i;
		    }
		}
	    }
	    if ( first > last ) {
	    	first = 1;
		last = 1;
	    }
	    closedir( ng );
	    if ( debugmode )
	    	syslog( LOG_DEBUG, "parsed directory %s: first %d, last %d",
			de->d_name, first, last );
	    insertgroup( de->d_name, first, last, 0, NULL );
	}
    }

    /* Repeat for local groups. Writing a wrapper might be more elegant */
    strcpy( s, spooldir );
    strcat( s, "/local.groups" );
    d = opendir( s );
    if ( !d ) {
	syslog( LOG_ERR, "cannot open directory %s: %m", s );
	return;
    }

    while ( (de = readdir(d)) ) {
	if ( isalnum((unsigned char)*(de->d_name) ) &&
	     chdirgroup(de->d_name, FALSE) ) {
	    first = INT_MAX;
	    last = 0;

	    ng = opendir( "." );
	    while ( ( nga = readdir( ng ) ) != NULL ) {
		if ( isdigit ((unsigned char) *(nga->d_name ) ) ) {
		    p = NULL;
		    i = strtoul( nga->d_name, &p, 10 );
		    if ( *p == '\0' ) {
			if ( i < first )
		    	    first = i;
			if ( i > last )
		    	    last = i;
		    }
		}
	    }
	    if ( first > last ) {
	    	first = 1;
		last = 1;
	    }
	    closedir( ng );
	    if ( debugmode )
	    	syslog( LOG_DEBUG, "parsed directory %s: first %d, last %d",
			de->d_name, first, last );
	    insertgroup( de->d_name, first, last, 0, NULL );
	}
    }

    mergegroups();

    closedir( d );
}
