/*
 * apply filter file to all files in a newsgroup
 *
 * Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 1999.
 *
 * Modified by Matthias Andree <matthias.andree@gmx.de>
 * Copyright of modifications 2000-2003.
 *
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "mastring.h"
#include "ln_log.h"
#include "msgid.h"

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

#define MAXHEADERSIZE 2047

extern char *optarg;
extern int optind, opterr, optopt;

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
	    "    -n: dry run, do not actually delete anything\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n",
	    sysconfdir);
}

/* read from fd into malloced buffer *bufp of size *size
 * up to delim or EOF.  Buffer is adjusted to fit input.
 * return pointer to match or end of file, NULL in case of error
 */
static /*@null@*/ /*@dependent@*/ char *
readtodelim(int fd, const char *name, /*@unique@*/ /*@observer@*/ const char *delim,
    char **bufp, size_t *size)
{
    size_t dlen = strlen(delim) - 1;
    size_t nread;
    ssize_t res;
    char *k;

    nread = 0;
    if (*size < 1 || *bufp == NULL)
	*bufp = critmalloc((*size = MAXHEADERSIZE+1), "readtodelim");

    /*@+loopexec@*/
    for (;;) {
	res = read(fd, *bufp + nread, *size - nread - 1);
	if (res < 0) { /* read error/EINTR/whatever */
	    printf("error reading %s: %s\n", name, strerror(errno));
	    return NULL;
	}

	(*bufp)[nread + res] = '\0';
	/* skip as much as possible */
	k = strstr(nread > dlen ? *bufp + nread - dlen : *bufp, delim);

	nread += res;
	if ((size_t)res < *size-nread-1) { /* FIXME: can short reads happen? */
	    return k != NULL ? k : *bufp + nread;
	}

	if (k != NULL) {
	    return k;
	}

	/* must read more */
	*bufp = critrealloc(*bufp, (*size)*=2, "readtodelim");
    }
    /*@=loopexec@*/
    /*@notreached@*/
    return NULL;
}

/* read article headers, cut off body
 * return 0 for success, -1 for error, -2 for article without body
 */
static int
readheaders(int fd, /*@unique@*/ const char *name, char **bufp, size_t *size)
{
    char *k = readtodelim(fd, name, "\n\n", bufp, size); /* XXX FIXME:
							    cater for wire
							    format */
    if (k != NULL) {
	if (*k == '\0')
	    return -2;
	else {
	    k[1] = '\0'; /* keep last header line \n terminated */
	    return 0;
	}
    } else {
	return -1;
    }
}

/* remove LF in LF+whitespace sequences, in place */
static void unfold(char *p)
{
    char *s, *q;

    q = p;
    s = p + strlen(p);
    while (p <= s) {
	if (p[0] != '\n' || p + 1 > s || !isspace((unsigned char)p[1])) {
	    *q++ = *p++;
	} else {
	    p++;
	}
    }
}

/* returns 0 for success */
static int applyfilter(const char *name, struct newsgroup *g,
	const struct filterlist *myfilter, int dryrun,
	unsigned long *kept, unsigned long *deleted)
{
    static size_t lsize = MAXHEADERSIZE + 1;
    static char *l;
    struct stat st;
    int score, fd;
    struct utimbuf u;
    unsigned long n;

    l = (char *)critmalloc(lsize, "Space for article");

    if (stat(name, &st)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		"cannot stat file \"%s\" in newsgroup %s: %m",
		g->name, name);
	return 0;
    }

    /* remove truncated files */
    if (st.st_size == 0)
	score = TRUE;

    if (!S_ISREG(st.st_mode)) {
	ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		"not a regular file in newsgroup %s: %s",
		g->name, name);
	return 0;
    }

    if((fd = open(name, O_RDONLY)) >= 0)
    {
	int ret;

	/* read and unfold headers */
	ret = readheaders(fd, name, &l, &lsize);
	if (ret != -1)
	    unfold(l);

	switch (ret) {
	    case 0:
		score = killfilter(myfilter, l);
		break;
	    case -1:
		score = TRUE;
		break;
	    case -2: /* article has no body */
		if (delaybody_group(g->name))
		    score = killfilter(myfilter, l);
		else
		    score = TRUE;
		break;
	    default:
		/*@notreached@*/
		score = FALSE;
	}
	close(fd);

	if (score) {
	    if (!dryrun) {
		char *msgid = NULL;
		if (strlen(l)) {
		    msgid = mgetheader("Message-ID:", l);
		    delete_article(msgid, "applyfilter", "filtered");
		} else {
		    unlink(name);
		}
	    } else { /* !dryrun */
		if (verbose) {
		    printf("%s would be deleted\n", name);
		}
	    }
	    (*deleted)++;
	} else {
	    /* restore atime and mtime to keep texpire
	     * functionality intact */
	    u.actime = st.st_atime;
	    u.modtime = st.st_mtime;
	    utime(name, &u);
	    (*kept) ++;
	}

	n = strtoul(name, NULL, 10);
	if (n) {
	    if (n < g->first)
		g->first = n;
	    if (n > g->last)
		g->last = n;
	}
    } else /* if (fd = open) >= 0 */
	ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		"could not open file \"%s\" in newsgroup %s\n",
		name, g->name);
    return 0;
}


static int
setupfilter(struct filterlist **myfilter, const char *ng) {
    if ((*myfilter = selectfilter(ng)) == NULL) {
	printf("Nothing to filter -- no filter for %s found.\n", ng);
	return 0;
    }
    return 1;
}

int
main(int argc, char *argv[])
{
    struct filterlist *myfilter;
    static const char c[] = "-\\|/";
    int i, option;
    unsigned long deleted, kept;
    char *conffile = NULL;
    DIR *d;
    struct dirent *de;
    struct newsgroup *g;
    int err, savedir;
    int dryrun = 0;
    int check = 0; /** if set, check if file given here would be filtered */

    savedir=open(".", O_RDONLY);

    ln_log_open("applyfilter");
    if (!initvars(argv[0], 0)) {
	fprintf(stderr, "%s: cannot initialize\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    while ((option = getopt(argc, argv, GLOBALOPTS "nc")) != -1) {
	if (parseopt("applyfilter", option, optarg, &conffile))
	    continue;
	switch (option) {
	    case 'n':
		dryrun = 1;
		break;
	    case 'c':
		dryrun = 1; /* imply -n */
		check = 1;
		break;
	    default:
		usage();
		/* fallthrough */
	    case ':':
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
    if (conffile)
	free(conffile);

    if (!filterfile || !readfilter(filterfile)) {
	printf("Nothing to filter -- no filterfile found.\n");
	exit(EXIT_FAILURE);
    }

    if (lockfile_exists(LOCKWAIT)) {
	fprintf(stderr, "%s: lockfile %s exists, abort\n", argv[0], lockfile);
	exit(EXIT_FAILURE);
    }

    rereadactive();

    if (check) {
	kept = deleted = 0;
	for(;optind<argc;optind++) {
	    FILE *inf;
	    char *ng;

	    if (savedir >= 0) (void)fchdir(savedir);
	    inf = fopen(argv[optind], "r");
	    if (!inf) {
		perror("applyfilter: fopen");
		continue;
	    }

	    ng = fgetheader(inf, "Newsgroups:", 1);
	    fclose(inf);
	    if (!ng) {
		printf("File %s does not look like a news article, skipping;\nNewsgroups: header is missing.\n", argv[optind]);
		continue;
	    }
	    g = findgroup(ng, active, -1);
	    if (!g) {
		printf("Newsgroups %s not found in active file.\n", ng);
		free(ng);
		continue;
	    }

	    if (setupfilter(&myfilter, ng))
		applyfilter(argv[optind], g, myfilter, dryrun, &kept, &deleted);
	    free(ng);
	}
	printf("%lu articles %sdeleted, %lu kept.\n", deleted,
		dryrun ? "would have been " : "",
		kept);
    } else {
	for (;optind<argc;optind++) {
	    kept = deleted = 0;
	    g = findgroup(argv[optind], active, -1);
	    if (!g) {
		printf("Newsgroups %s not found in active file.\n", argv[optind]);
		continue;
	    }
	    g->first = ULONG_MAX;
	    if (!chdirgroup(g->name, FALSE)) {
		printf("No articles for newsgroup: %s\n", g->name);
		continue;
	    }
	    if (setupfilter(&myfilter, g->name) == 0)
		continue;
	    if (!(d = opendir("."))) {
		printf("Unable to open directory for newsgroup %s\n", g->name);
		unlink(lockfile);
		continue;
	    }
	    i = 0;
	    deleted = 0;
	    kept = 0;
	    ln_log(LNLOG_SINFO, LNLOG_CTOP, "Applying filters to %s...", g->name);
	    while ((de = readdir(d)) != NULL) {
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

		applyfilter(de->d_name, g, myfilter, dryrun, &kept, &deleted);
	    } /* while readdir */
	    closedir(d);
	    if (g->first > g->last) {
		/* group is empty */
		g->first = g->last + 1;
	    }
	    if (deleted) {
		if (verbose)
		    printf("Updating .overview file...");
		getxover(1);
	    }
	    printf("%lu articles %sdeleted, %lu kept.\n", deleted,
		    dryrun ? "would have been " : "",
		    kept);
	}
    }
    writeactive();
    unlink(lockfile);
    if (verbose)
	printf("Done.\n");
    freexover();
    freeactive(active);
    exit(EXIT_SUCCESS);
}
