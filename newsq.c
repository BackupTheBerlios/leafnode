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
    unsigned long filesize;
    long count = 0;

    if (chdir(s) < 0) {
	fprintf(stderr, "Cannot change to %s -- aborting.\n", s);
	return (-1);
    }
    d = opendir(".");
    if (!d) {
	fprintf(stderr, "Cannot open directory %s -- aborting.\n", s);
	return (-1);
    }
    while ((de = readdir(d))) {
	if (stat(de->d_name, &st)) {
	    fprintf(stderr, "Cannot stat %s\n", de->d_name);
	} else if (S_ISREG(st.st_mode)) {
	    f = fopen(de->d_name, "r");
	    if (f) {
		char *fr, *ng, *su;
		filesize = st.st_size;
		count++;
		printf("%s: %8lu bytes, spooled %s\tFrom: %-.66s\n"
		       "\tNgrp: %-.66s\n\tSubj: %-.66s\n",
		       de->d_name, filesize, ctime(&st.st_mtime),
		       fr = fgetheader(f, "From:", 1),
		       ng = fgetheader(f, "Newsgroups:", 1),
		       su = fgetheader(f, "Subject:", 1));
		fclose(f);
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
    closedir(d);
    return count;
}

int
main(int argc, char **argv)
{
    char s[PATH_MAX + 1];	/* FIXME */
    int option;
    int ret = 0;
    long c;

    while ((option = getopt(argc, argv, "VD")) != -1) {
	if (!parseopt("newsq", option, NULL, NULL, 0)) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    printf("Articles awaiting post to upstream servers:\n"
	   "-------------------------------------------\n");
    sprintf(s, "%.*s/out.going", (int)sizeof(s) - 20, spooldir);
    if ((c = show_queue(s)) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1 ? "" : "s");
    }


    printf("\n");
    printf("Articles awaiting store to local groups:\n"
	   "----------------------------------------\n");
    sprintf(s, "%.*s/in.coming", (int)sizeof(s) - 20, spooldir);
    if ((c = show_queue(s)) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1 ? "" : "s");
    }

    exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
}
