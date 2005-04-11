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
#include <sysexits.h>

static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "newsq [OPTIONS] [-cf]\n"
	    "options are:\n"
	    GLOBALOPTLONGHELP
	    "    -c             - exit with code %d on error,\n"
	    "                     exit with code %d if out.going queue has articles\n"
	    "                     exit with code %d if out.going queue is empty.\n"
	    "    -f             - print queue of articles to be fetched in delaybody mode\n"
	    "    -h             - print short usage help like this\n",
	    EXIT_FAILURE, EXIT_SUCCESS, EX_UNAVAILABLE);
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
		char *mi = fgetheader(f, "Message-ID:", 1);
		char *da = fgetheader(f, "Date:", 1);
		filesize = st.st_size;
		count++;
		if (fr != NULL && ng != NULL && su != NULL) {
		    printf("%s: %8lu bytes, %sspooled %s"
			   "\tFrom: %-.66s\n"
			   "\tNgrp: %-.66s\n"
			   "\tDate: %-.66s\n"
			   "\tSubj: %-.66s\n"
			   "\tM-ID: %-.66s\n",
			   de->d_name, (unsigned long)filesize,
			   S_IXUSR & st.st_mode ? "posted, " : "",
			   ctime(&st.st_mtime),
			   fr, ng, da, su, mi);
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
    int option, check = 0, delayed = 0;
    int ret = 0;
    long c;
    mastr *s = mastr_new(LN_PATH_MAX);
    const char *const myname = "newsq";

    if (!initvars(argv[0], 0))
	init_failed(myname);

    while ((option = getopt(argc, argv, GLOBALOPTS "cfh")) != -1) {
	if (parseopt(myname, option, optarg, NULL))
	    continue;
	switch(option) {
	    case 'c':
		check = 1;
		break;
	    case 'f':
		delayed = 1;
		break;
	    case 'h':
		usage();
		exit(EXIT_SUCCESS);
	    default:
		usage();
		exit(EXIT_FAILURE);
	}
    }

    if (!init_post())
	init_failed(myname);

    if (check) {
	unsigned long count;
	char **list = spooldirlist_prefix("out.going", DIRLIST_NONDOT, &count);
	if (!list) exit(EXIT_FAILURE);
	free_dirlist(list);
	exit(count ? EXIT_SUCCESS : EX_UNAVAILABLE);
    }

    if (delayed) {
	RBLIST *r;
	const char *i;
	mastr *fname = mastr_new(LN_PATH_MAX);

	if (!initinteresting() || !(r = openinteresting())) {
	    fprintf(stderr, "error: Cannot read interesting.groups directory: %s",
		    strerror(errno));
	    mastr_delete(fname);
	    exit(EXIT_FAILURE);
	}

	while ((i = readinteresting(r))) {
	    FILE *f;
	    char *l;

	    (void)mastr_clear(fname);
	    (void)mastr_vcat(fname, spooldir, "/interesting.groups/", i,  NULL);
	    if (!(f = fopen(mastr_str(fname), "r"))) {
		fprintf(stderr,
			"error: Cannot open %s for reading: %s", mastr_str(fname),
			strerror(errno));
		continue;
	    }
	    while ((l = getaline(f))) {
		char *fi[4];

		if (str_nsplit(fi, l, " ", COUNT_OF(fi)) < 2) {
		    /* skip malformatted line */
		    fprintf(stderr,
			    "warning: skipping malformatted "
			    "interesting.groups/%s line \"%s\"",
			    i, l);
		    continue;
		}

		printf("%s:%s %s\n", i, fi[1], fi[0]);
	    }
	    fclose(f);
	}
	mastr_delete(fname);

	closeinteresting(r);
	exit(EXIT_SUCCESS);
    }

    printf("Articles awaiting post to upstream servers:\n"
	   "-------------------------------------------\n");
    mastr_vcat(s, spooldir, "/out.going", NULL);
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
    mastr_vcat(s, spooldir, "/in.coming", NULL);
    if ((c = show_queue(mastr_str(s))) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1l ? "" : "s");
    }
    mastr_clear(s);

    printf("\n");
    printf("Articles in failed.postings:\n" "----------------------------\n");
    mastr_vcat(s, spooldir, "/failed.postings", NULL);
    if ((c = show_queue(mastr_str(s))) < 0) {
	printf("Error occurred.\n");
	ret = 1;
    } else {
	printf("%ld article%s found.\n", c, c == 1l ? "" : "s");
    }
    mastr_delete(s);

    exit(ret ? EXIT_FAILURE : EXIT_SUCCESS);
}
