/*
 * apply filter file to all files in a newsgroup
 *
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1999.
 *
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define MAXHEADERSIZE 5000

extern char *optarg;
extern int optind, opterr, optopt;

int debug = 0;

static void
usage(void)
{
    fprintf(stderr,
	    "Usage:\n"
	    "applyfilter -V: print version number and exit\n"
	    "applyfilter [-Dv] [-F configfile] newsgroups\n"
	    "    -D: switch on debug mode\n"
	    "    -v: switch on verbose mode\n"
	    "    -F: use \"configfile\" instead of %s/config\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n",
	    libdir);
}

int
main(int argc, char *argv[])
{
    struct filterlist *myfilter;
    const char c[] = "-\\|/";
    int i, score, option, deleted, kept;
    unsigned long n;
    char *l, *msgid;
    /*@dependent@*/ char *k;
    const char *msgidpath = "";
    int f;
    DIR *d;
    struct dirent *de;
    struct stat st;
    struct utimbuf u;
    struct newsgroup *g;
    char conffile[PATH_MAX];
    int err;

    err = snprintf(conffile, sizeof(conffile), "%s/config", libdir);
    if (err < 0 || err >= (int)sizeof(conffile)) {
	/* overflow */
	fprintf(stderr, "caught string overflow in configuration file name\n");
	exit(EXIT_FAILURE);
    }

    ln_log_open("applyfilter");
    if (!initvars(argv[0])) {
	fprintf(stderr, "%s: cannot initialize\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    while ((option = getopt(argc, argv, "F:D:Vv")) != -1) {
	if (!parseopt("applyfilter", option, optarg, conffile,
		      sizeof(conffile))) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    if (optind + 1 > argc) {
	usage();
	exit(EXIT_FAILURE);
    }

    if ((err = readconfig(conffile)) != 0) {
	printf("Reading configuration failed (%s).\n", strerror(err));
	exit(2);
    }

    if (!filterfile || !readfilter(filterfile)) {
	printf("Nothing to filter -- no filterfile found.\n");
	exit(EXIT_FAILURE);
    }

    if ((myfilter = selectfilter(argv[optind])) == NULL) {
	printf("Nothing to filter -- no regexp for %s found.\n", argv[optind]);
	exit(EXIT_SUCCESS);
    }

    whoami();

    if (lockfile_exists(FALSE))
	exit(EXIT_FAILURE);
    rereadactive();
    g = findgroup(argv[optind], active, -1);
    if (!g) {
	printf("Newsgroups %s not found in active file.\n", argv[optind]);
	unlink(lockfile);
	exit(EXIT_FAILURE);
    }
    g->first = ULONG_MAX;
    g->last = 0ul;
    if (!chdirgroup(g->name, FALSE)) {
	printf("No such newsgroup: %s\n", g->name);
	unlink(lockfile);
	exit(EXIT_FAILURE);
    }
    if (!(d = opendir("."))) {
	printf("Unable to open directory for newsgroup %s\n", g->name);
	unlink(lockfile);
	exit(EXIT_FAILURE);
    }
    i = 0;
    deleted = 0;
    kept = 0;
    l = (char *)critmalloc(MAXHEADERSIZE + 1, "Space for article");
    while ((de = readdir(d)) != NULL) {
	msgid = 0;
	if (!isdigit((unsigned char)de->d_name[0])) {
	    /* no need to stat file */
	    continue;
	}
	switch (verbose) {
	case 1:{
		printf("%c", c[i % 4]);
		fflush(stdout);
		i++;
		break;
	    }
	case 2:{
		printf("%s\n", de->d_name);
	    }
	}
	stat(de->d_name, &st);
	if (S_ISREG(st.st_mode)
	    && (f = open(de->d_name, O_RDONLY))) {
	    if (read(f, l, MAXHEADERSIZE) < 0) {
		printf("error reading %s: %s\n", de->d_name, strerror(errno));
	    }
	    close(f);
	    msgid = mgetheader("Message-ID:", l);
	    if ((k = strstr(l, "\n\n")) != NULL) {
		*k = '\0';	/* cut off body */
		score = killfilter(myfilter, l);
	    } else {
		/* article has no body - delete it */
		score = TRUE;
	    }
	    if (score) {
		unlink(de->d_name);
		/* delete stuff in message.id directory as well */
		if (msgid) {
		    msgidpath = lookup(msgid);
		    if ((stat(msgidpath, &st) == 0)
			&& (st.st_nlink < 2)) {
			if (unlink(msgidpath) == 0)
			    deleted++;
		    }
		}
		if (verbose)
		    printf("%s %s deleted\n", de->d_name, msgidpath);
	    } else {
		n = strtoul(de->d_name, NULL, 10);
		if (n) {
		    if (n < g->first)
			g->first = n;
		    if (n > g->last)
			g->last = n;
		}
		u.actime = st.st_atime;
		u.modtime = st.st_mtime;
		utime(de->d_name, &u);
		kept++;
	    }
	} else
	    printf("could not open %s\n", de->d_name);
	if (msgid)
	    free(msgid);
    }
    closedir(d);
    free(l);
    if (g->first <= g->last)
	writeactive();
    unlink(lockfile);
    printf("%d articles deleted, %d kept.\n", deleted, kept);
    if (verbose)
	printf("Updating .overview file\n");
    getxover(1);
    exit(0);
}
