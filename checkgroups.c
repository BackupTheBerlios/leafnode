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

static void
process_input(char *s)
{
    FILE *f;
    char *l;
    unsigned long changed = 0, counted = 0;

    f = fopen(s, "r");
    if (!f) {
	fprintf(stderr, "cannot open %s: %s\n", s, strerror(errno));
	return;
    }
    ln_log(LNLOG_SINFO, LNLOG_CTOP, "processing checkgroups file %s", s);
    while ((l = getaline(f))) {
	char *p;
	struct newsgroup *g;

	p = l;
	if (isalnum((unsigned char)*p)) {
	    SKIPWORDNS(p);
	    if (*p)
		*p++ = '\0';
	    if ((g = findgroup(l, active, -1)) != NULL) {
		if (verbose)
		    puts(l);
		if (strlen(p) > 0 && strcmp(g->desc,p)) {
		    if (g->desc)
			free(g->desc);
		    g->desc = critstrdup(p, "process_input");
		    changed++;
		}
	    }
	}
    }
    ln_log(LNLOG_SINFO, LNLOG_CTOP, "updated %lu description%s from %lu in %s",
	    changed, changed == 1 ? "" : "s", counted, s);
    fclose(f);
}

static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "checkgroups [options] checkfile [...]\n"
	    "options are:\n"
	    GLOBALOPTLONGHELP
	   );
}

int
main(int argc, char *argv[])
{
    int option, reply;
    const char *const myname = "checkgroups";
    char *conffile = NULL;

    ln_log_open(myname);
    if (!initvars(argv[0]))
	init_failed(myname);
    while ((option = getopt(argc, argv, GLOBALOPTS)) != -1) {
	if (!parseopt(myname, option, optarg, &conffile)) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }
    if (optind == 0 || optind == argc) {
	usage();
	exit(EXIT_FAILURE);
    }

    if ((reply = readconfig(conffile)) != 0) {
	printf("Reading configuration failed (%s).\n", strerror(reply));
	exit(2);
    }
    if (conffile)
	free(conffile);

    if (!init_post(0))
	init_failed(myname);

    umask((mode_t)07);

    /* lock */
    if (attempt_lock(LOCKWAIT)) {
	fprintf(stderr, "%s: lockfile %s exists, abort\n", argv[0], lockfile);
	exit(EXIT_FAILURE);
    }
    rereadactive();		/* read groupinfo file */
    while(optind < argc) {
	process_input(argv[optind]);
	optind++;
    }
    writeactive();		/* write groupinfo file */
    freeactive(active);
    (void)log_unlink(lockfile, 0);	/* unlock */
    exit(0);
}
