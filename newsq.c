/*
 * find out what is in your out.going directory
 *
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1999.
 * Rewritten by Matthias Andree <matthias.andree@gmx.de>. Copyright 2001.
 *
 * See README for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "mastring.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

int debug = 0;
static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "newsq -V:\n"
	    "    print version on stderr and exit\n"
	    "newsq [-D] message-id\n"
	    "    -D: switch on debugmode\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n");
}

static long
show_queue(const char *s)
{
    struct dirent *de;
    FILE *f;
    DIR *d;
    struct stat st;
    off_t filesize;
    long count = 0;

    if (chdir(s) < 0) {
	fprintf(stderr, "Cannot change to %s -- aborting.\n", s);
	return -1l;
    }
    d = opendir(".");
    if (!d) {
	fprintf(stderr, "Cannot open directory %s -- aborting.\n", s);
	return -1l;
    }
    while ((de = readdir(d))) {
	if (stat(de->d_name, &st)) {
	    fprintf(stderr, "Cannot stat %s\n", de->d_name);
	} else if (S_ISREG(st.st_mode)) {
	    f = fopen(de->d_name, "r");
	    if (f) {
		char *fr = fgetheader(f, "From:", 1);
		char *ng = fgetheader(f, "Newsgroups:", 1);
		char *su = fgetheader(f, "Subject:", 1);
		filesize = st.st_size;
		count++;
		if (fr != NULL && ng != NULL && su != NULL) {
		    printf("%s: %8lu bytes, spooled %s\tFrom: %-.66s\n"
			   "\tNgrp: %-.66s\n\tSubj: %-.66s\n",
			   de->d_name, (unsigned long)filesize,
			   ctime(&st.st_mtime), fr, ng, su);
		} else {
		    fprintf(stderr, "Header missing in file %s\n", de->d_name);
		}
		(void)fclose(f);
		if (fr)
		    free(fr);
		if (ng)
		    free(ng);
		if (su)
		    free(su);
	    } else
		fprintf(stderr, "Cannot open %s\n", de->d_name);
	}
    }
    (void)closedir(d);
    return count;
}

int
main(int argc, char **argv)
{
    int option;
    int ret = 0;
    long c;
    mastr *s = mastr_new(PATH_MAX);

    while ((option = getopt(argc, argv, "VD:")) != -1) {
	if (!parseopt("newsq", option, NULL, NULL, 0)) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    printf("Articles awaiting post to upstream servers:\n"
	   "-------------------------------------------\n");
    mastr_vcat(s, spooldir, "/out.going", 0);
    if ((c = show_queue(mastr_str(s))) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1l ? "" : "s");
    }
    mastr_clear(s);

    printf("\n");
    printf("Articles awaiting store to local groups:\n"
	   "----------------------------------------\n");
    mastr_vcat(s, spooldir, "/in.coming", 0);
    if ((c = show_queue(mastr_str(s))) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1l ? "" : "s");
    }
    mastr_clear(s);

    printf("\n");
    printf("Articles in failed.postings:\n" "----------------------------\n");
    mastr_vcat(s, spooldir, "/failed.postings", 0);
    if ((c = show_queue(mastr_str(s))) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1l ? "" : "s");
    }
    mastr_delete(s);

    exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
}
