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

#define MAXHEADERSIZE 2047

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
	    "    -n: dry run, do not actually delete anything\n"
	    "See also the leafnode homepage at http://www.leafnode.org/\n",
	    libdir);
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
    char *k = readtodelim(fd, name, "\n\n", bufp, size);
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

int
main(int argc, char *argv[])
{
    struct filterlist *myfilter;
    static const char c[] = "-\\|/";
    int i, score, option, deleted, kept;
    unsigned long n;
    char *l;
    size_t lsize;
    const char *msgidpath = "";
    int fd;
    DIR *d;
    struct dirent *de;
    struct stat st;
    struct utimbuf u;
    struct newsgroup *g;
    char conffile[PATH_MAX];
    int err;
    int dryrun = 0;

    err = snprintf(conffile, sizeof conffile, "%s/config", libdir);
    if (err < 0 || err >= (int)sizeof conffile) {
	/* overflow */
	fprintf(stderr, "caught string overflow in configuration file name\n");
	exit(EXIT_FAILURE);
    }

    ln_log_open("applyfilter");
    if (!initvars(argv[0], 0)) {
	fprintf(stderr, "%s: cannot initialize\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    while ((option = getopt(argc, argv, "F:D:nVv")) != -1) {
	if (parseopt("applyfilter", option, optarg, conffile, sizeof conffile))
	    continue;
	switch (option) {
	case 'n':
	    dryrun = 1;
	    break;
	default:
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

    if (lockfile_exists(LOCKWAIT)) {
	fprintf(stderr, "%s: lockfile %s exists, abort\n", argv[0], lockfile);
	exit(EXIT_FAILURE);
    }

    rereadactive();
    g = findgroup(argv[optind], active, -1);
    if (!g) {
	printf("Newsgroups %s not found in active file.\n", argv[optind]);
	unlink(lockfile);
	exit(EXIT_FAILURE);
    }
    g->first = ULONG_MAX;
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
    lsize = MAXHEADERSIZE + 1;
    l = (char *)critmalloc(lsize, "Space for article");
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
	stat(de->d_name, &st);
	if (S_ISREG(st.st_mode)
	    && (fd = open(de->d_name, O_RDONLY))) {
	    switch (readheaders(fd, de->d_name, &l, &lsize)) {
	    case 0:
		score = killfilter(myfilter, l);
		break;
	    case -1:
		score = TRUE;
		break;
	    case -2: /* article has no body */
		if (delaybody)
		    score = killfilter(myfilter, l);
		else
		    score = TRUE;
		break;
	    default:
		/*@notreached@*/
		score = FALSE;
	    }
	    close(fd);

	    if (score && !dryrun) {
		char *msgid = mgetheader("Message-ID:", l);
		unlink(de->d_name);
		/* delete stuff in message.id directory as well */
		if (msgid) {
		    msgidpath = lookup(msgid);
		    if ((stat(msgidpath, &st) == 0)
			&& (st.st_nlink < 2)) {
			if (unlink(msgidpath) == 0)
			    deleted++;
		    }
		    free(msgid);
		}
		if (verbose)
		    printf("%s %s deleted\n", de->d_name, msgidpath);
	    } else {
		if (score && dryrun && verbose) {
		    printf("%s would be deleted\n", de->d_name);
		}
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
    }
    closedir(d);
    free(l);
    writeactive();
    unlink(lockfile);
    printf("%d articles deleted, %d kept.\n", deleted, kept);
    if (verbose)
	printf("Updating .overview file\n");
    getxover(1);
    exit(EXIT_SUCCESS);
}
