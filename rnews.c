/*
 * rnews: sort articles or UUCP batchfiles into spool
 *
 * Written and copyrighted by Cornelius Krasel, January-April 1999
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#define GZIP "/bin/gzip"

extern int optind;

int debug = 0;

int processfile ( char * filename );

static void usage( void ) {
    fprintf( stderr,
	"Usage:\n"
	"rnews -V\n"
	"    print version on stderr and exit\n"
	"rnews [-vD] [file|directory]\n"
	"    -D: switch on debugmode\n"
	"    -v: verbose mode\n"
	"See also the leafnode homepage at http://www.leafnode.org/\n"
    );
}

/*
 * process an uncompressed newsbatch
 * return 1 if successful, 0 otherwise
 */
static int processbatch( char * filename ) {
    FILE * f, * artfile;
    char * l, * artname;
    long bytes;

    f = fopen( filename, "r" );
    if ( !f ) {
	fprintf( stderr, "unable to open batchfile %s -- abort\n", filename );
	return 0;
    }
    artname = tmpnam( NULL ); /* FIXME */
    if ( !artname ) {
	fprintf( stderr, "unable to create temporary article -- abort\n" );
	fclose( f );
	return 0;
    }
    while ( ( l = getaline( f ) ) ) {
	const char tomatch[]="#! rnews ";
	if(strncmp(l, tomatch, strlen(tomatch))) {
		fprintf(stderr, "expected `#! rnews', got `%-.40s[...]' ", 
			l);
		return 0;
	}
	if(!get_long(l+strlen(tomatch),&bytes)) {
		fprintf(stderr, "cannot extract article length from
`%-.40s[...]' ", l);
		return 0;
	}
	artfile = fopen( artname, "w" );
	copyfile( f, artfile, bytes );
	fclose( artfile );
	processfile( artname );
    }
    unlink( artname );
    fclose( f );
    return 1;
}

/*
 * copy a compressed newsbatch to tmp
 * return NULL if unsuccessful, otherwise name of compressed file without suffix
 */
static char * makecompressedfile( FILE * infile ) {
    FILE * outfile ;
    char * c, * buf;
    size_t i;

    c = tmpnam( NULL ); /* FIXME */
    if ( !c ) {
	fprintf( stderr, "unable to create temporary file -- abort\n" );
	return NULL;
    }
    sprintf( s, "%s.gz", c );
    outfile = fopen( s, "r" );
    if ( outfile ) {
	fprintf( stderr, "temporary file %s already exists -- abort\n",
		 s );
	fclose( outfile );
	return NULL;
    }
    outfile = fopen( s, "w" );
    if ( !outfile ) {
	fprintf( stderr, "unable to open temporary file %s -- abort\n",
		 s );
	return NULL;
    }
    buf = critmalloc( BLOCKSIZE+1, "Allocating memory" );
    do {
	i = fread( buf, sizeof(char), BLOCKSIZE, infile );
	if ( i < BLOCKSIZE && !feof( infile ) ) {
	    fprintf( stderr, "read error on compressed batch\n" );
	    clearerr( infile );
	}
	fwrite( buf, sizeof(char), i, outfile );
    } while ( i == BLOCKSIZE );
    free( buf );
    fclose( outfile );
    return strdup(c);
}

/*
 * uncompress a file in filename.gz
 * return 1 if successful, 0 if not
 */
static int uncompressfile( char * filename ) {
    pid_t pid;
    int   statloc;

    pid = fork();
    switch ( pid ) {
	case 0: {
	    /* child process uncompresses stuff */
	    sprintf( s, "%s.gz", filename );
	    execl( GZIP, GZIP, "-d", s, NULL );
	    /* if this is reached, calling gzip has failed */
	    _exit(99);
	}
	case -1: {
	    fprintf( stderr, "unable to fork()\n" );
	    return 1;
	}
	default: break;
    }
    waitpid( pid, &statloc, 0 );

    if ( WIFEXITED( statloc ) == 0 ) {
	fprintf( stderr, "Decompressing failed: child crashed.\n" );
	unlink( s );
	return 0;
    } else if ( WEXITSTATUS( statloc ) == 99 ) {
	fprintf( stderr, "Decompressing failed: " GZIP " crashed.\n" );
	unlink( s );
	return 0;
    } else if ( WEXITSTATUS( statloc ) ) {
	fprintf( stderr, "Decompressing failed.\n" );
	unlink( s );
	return 0;
    }
    return 1;
}

/*
 * process any file
 * return 0 if failed, 1 otherwise
 */
static int fprocessfile ( FILE * f, char * filename ) {
    char * l;
    char * tempfilename ;
    int  i;
    
    debug = 0;
    l = getaline( f );
    debug = debugmode;
    if ( !l || strlen(l) < 2) {
	fprintf( stderr, "%s is corrupted: %s -- abort\n", filename, l );
	fclose( f );
	return 0;
    }
    if (( l[0] == '#' ) && ( l[1] == '!' )) {
	char * c;

	c = l+2;
	while ( *c && isspace((unsigned char)(*c)) )
	    c++;
	
	if ( strncmp( c, "rnews", 5 ) == 0 ) {
	    fclose( f );
	    return processbatch( filename );
	} else if ( ( strncmp( c, "cunbatch", 8 ) == 0 ) ||
		    ( strncmp( c, "zunbatch", 8 ) == 0 ) ) {
	    tempfilename = makecompressedfile( f );
	    fclose( f );
	    if ( !uncompressfile( tempfilename ) )
		return 0;
	    i = processfile( tempfilename );
	    unlink( tempfilename );
	    return i;
	} else {
	    fclose( f );
	    fprintf( stderr, "Don't know how to handle %s\n", c );
	    return 0;
	}
    }

    storearticle( filename, fgetheader( f, "Message-ID:" ),
		  fgetheader( f, "Newsgroups:" ) );
    fclose( f );
    return 1;
}

/*
 * process any file
 * return 0 if failed, 1 otherwise
 */
int processfile ( char * filename ) {
    FILE * f;

    if ( ( f = fopen( filename, "r" ) ) == NULL ) {
        fprintf( stderr, "unable to open %s: %m\n", filename );
        return 0;
    }
    return fprocessfile( f, filename );	/* f is closed in fprocessfile */
}

static void processdir ( char * pathname ) {
    DIR * dir;
    struct dirent * d;
    char *procdir;

    if ( chdir(pathname) < 0 ) {
	fprintf( stderr, "cannot chdir to %s\n", pathname );
	return;
    }
    procdir = getcwd( NULL, 0 );
    if ( ( dir = opendir( "." ) ) == NULL ) {
	fprintf( stderr, "cannot open %s\n", pathname );
	return;
    }
    while ( ( d = readdir( dir ) ) != NULL ) {
	/* don't process dotfiles */
	if ( d->d_name[0] != '.' ) {
	    (void)processfile( d->d_name );
	    /* storearticle uses chdir! */
	    if ( chdir( procdir ) < 0 ) {
		/* Yes, I'm paranoid */
		fprintf( stderr, "cannot chdir to %s\n", pathname );
		return;
	    }
	}
    }
    closedir( dir );
    free( procdir );
}

int main(int argc, char *argv[] ) {
    char * ptr;
    char option;
    struct stat st;

    if ( !initvars( argv[0] ) )
	exit(EXIT_FAILURE);

#ifdef HAVE_OLD_SYSLOG
    openlog( argv[0], LOG_PID );
#else
    openlog( argv[0], LOG_PID|LOG_CONS, LOG_NEWS );
#endif

    while ( ( option = getopt( argc, argv, "DVv" )) != -1 ) {
	if ( !parseopt( "rnews", option, NULL, NULL ) ) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }
    debug = debugmode;

    whoami();
    umask(2);
    if ( lockfile_exists( FALSE, FALSE ) )
	exit(EXIT_FAILURE);
    readactive();
    readlocalgroups();

    if ( !argv[optind] )
	fprocessfile( stdin, "stdin" );     /* process stdin */

    while ( ( ptr = argv[ optind++ ] ) != NULL ) {
	if ( stat( ptr, &st ) == 0 ) {
	    if ( S_ISDIR( st.st_mode ) )
		processdir( ptr );
	    else if ( S_ISREG( st.st_mode ) )
		processfile( ptr );
	    else
		fprintf( stderr, "%s: cannot open %s\n", argv[0], ptr );
	}
	else
	    fprintf( stderr, "%s: cannot stat %s\n", argv[0], ptr );
    }

    writeactive();		/* write groupinfo file */
    fixxover();			/* fix xoverview files */
    unlink( lockfile );

    exit(0);
}
