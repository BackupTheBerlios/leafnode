/*
 * Checkgroups: script that updates the description of newsgroups
 * Input file : a checkgroups script that contains the name of
 *              the newsgroup and its description in one line
 *
 * Written and copyrighted by Cornelius Krasel, April 1997
 * Source code borrows a lot from fetch(1).
 *
 * See README for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include <sys/types.h>
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

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

static void process_input(char *s);
void
process_input(char *s)
{
    FILE *f;
    char *l;

    f = fopen(s, "r");
    if (!f) {
	fprintf(stderr, "%s deleted (shouldn't happen)\n", s);
	return;
    }
    while ((l = getaline(f))) {
	char *p;
	struct newsgroup *g;

	p = l;
	if (isalnum((unsigned char)*p)) {
	    SKIPWORDNS(p);
	    if (*p)
		*p++ = '\0';
	    if ((g = findgroup(l, active, -1)) != NULL) {
		fprintf(stderr, "%s\n", l);
		if (strlen(p) > 0) {
		    if (g->desc)
			free(g->desc);
		    g->desc = critstrdup(p, "process_input");
		}
	    }
	}
    }
    fclose(f);
}

static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "checkgroups -V: print version number and exit\n"
	    "checkgroups [-Dv] checkfile\n"
	    "    -D: switch on debug mode\n"
	    "    -v: switch on verbose mode\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n");
}

int
main(int argc, char *argv[])
{
    int option;
    FILE *f;

    ln_log_open("checkgroups");
    if (!initvars(argv[0], 0))
	exit(EXIT_FAILURE);
    while ((option = getopt(argc, argv, "D:Vv")) != -1) {
	if (!parseopt("checkgroups", option, NULL, NULL)) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }
    if (optind == 0 || optind == argc) {
	usage();
	exit(EXIT_FAILURE);
    }
    /* Check whether input file exists */
    if (!(f = fopen(argv[optind], "r"))) {
	if (errno == EACCES)
	    fprintf(stderr, "%s: not permitted to open %s\n", argv[0], argv[1]);
	else
	    fprintf(stderr, "%s: checkgroups file %s doesn't exist\n",
		    argv[0], argv[1]);
	exit(EXIT_FAILURE);
    } else {
	fclose(f);
    }

    umask((mode_t)2);

    /* lock */
    if (lockfile_exists(LOCKWAIT)) {
	fprintf(stderr, "%s: lockfile %s exists, abort\n", argv[0], lockfile);
	exit(EXIT_FAILURE);
    }
    rereadactive();		/* read groupinfo file */
    process_input(argv[1]);
    writeactive();		/* write groupinfo file */
    freeactive(active);
    (void)log_unlink(lockfile, 0);	/* unlock */
    exit(0);
}
