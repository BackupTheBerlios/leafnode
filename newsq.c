/*
 * find out what is in your out.going directory
 *
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1999.
 *
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int debug = 0;

static void usage( void ) {
    fprintf( stderr,
	"Usage:\n"
	"newsq -V:\n"
	"    print version on stderr and exit\n"
	"newsq [-D] message-id\n"
	"    -D: switch on debugmode\n"
	"See also the leafnode homepage at http://www.leafnode.org/\n"
    );
}

int main ( int argc, char ** argv ) {
    FILE * f;
    DIR * d;
    struct dirent * de;
    int option;
    struct stat st;
    unsigned long filesize;

    while ( ( option =getopt( argc, argv, "VD" ) ) != -1 ) {
	if ( !parseopt( "newsq", option, NULL, NULL ) ) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    sprintf( s, "%s/out.going", spooldir );
    if ( chdir(s) < 0 ) {
	fprintf( stderr, "Cannot change to %s -- aborting.\n", s );
	exit(EXIT_FAILURE);
    }
    d = opendir( "." );
    if ( !d ) {
	fprintf( stderr, "Cannot open directory %s -- aborting.\n", s );
	exit(EXIT_FAILURE);
    }

    while ( ( de = readdir(d) ) ) {
	if ( stat( de->d_name, &st ) ) {
	    fprintf( stderr, "Cannot stat %s\n", de->d_name );
	}
	else if ( S_ISREG( st.st_mode ) ) {
	    f = fopen( de->d_name, "r" );
	    if ( f ) {
	    	filesize = st.st_size;
		printf("%s: %8lu bytes, spooled %s\tFrom: %-.66s\n"
			"\tNgrp: %-.66s\n\tSubj: %-.66s\n",
			de->d_name, filesize, ctime(&st.st_mtime),
			fgetheader( f, "From:" ),
			fgetheader( f, "Newsgroups:" ),
			fgetheader( f, "Subject:" ) );
		fclose(f);
	    }
	    else
		fprintf( stderr, "Cannot open %s\n", de->d_name );
	}
    }
    exit(0);
}
