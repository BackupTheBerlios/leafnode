/*
 * rnews: sort articles or UUCP batchfiles into spool
 *
 * Written and copyrighted by Cornelius Krasel, January-April 1999
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "get.h"
#include "critmem.h"
#include "mastring.h"
#include "ln_log.h"

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
#include <unistd.h>
#include <errno.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/* extern int optind; */
static int processfile(char *filename);
static void
usage(void)
{
    printf("Usage:\n"
	   "rnews -V\n"
	   "    print version on stderr and exit\n"
	   "rnews [-vD] [file|directory]\n"
	   "    -D: switch on debugmode\n"
	   "    -v: verbose mode\n"
	   "See also the leafnode homepage at http://www.leafnode.org/\n");
}

/*
 * process an uncompressed newsbatch
 * return 1 if successful, 0 otherwise
 */
static int
fprocessbatch(FILE * f, char *firstline)
{
    char *l;
    long bytes;
    int ret;

    while ((l = (firstline ? firstline : getaline(f)))) {
	static const char tomatch[] = "#! rnews ";
	firstline = NULL;
	ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
		"fprocessbatch: read \"%s\"", l);

	if (!str_isprefix(l, tomatch)) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "expected `#! rnews', got %-.40s%s", l,
		   strlen(l) > 40 ? "[...]" : "");
	    goto pb_bail;
	}
	if (0 == get_long(l + strlen(tomatch), &bytes)) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "cannot extract article length from `%-.40s%s'\n", l,
		   strlen(l) > 40 ? "[...]" : "");
	    goto pb_bail;
	}
	ret = store_stream(f, 0, filter, bytes, 0);
	ln_log(LNLOG_SDEBUG, LNLOG_CARTICLE,
			"store_stream returns %d", ret);
    }
    return 1;
  pb_bail:
    return 0;
}

/* 0: problem
 * FILE pointer: ok
 */
static FILE *
decompresspipe(FILE *original, const char *const compressor[])
{
    /* plan: use a "feeder" process to read from original and pipe into
     * the "exploder" process. (pipe #1)
     * use an "exploder" process to decompress data, and read from its
     * output the file (pipe #2).
     * this function returns the end of pipe #2.
     */
    pid_t pidfeeder, pidexploder;
    int feedpipe[2];
    int explodepipe[2];
    FILE *f;
    char *argv[10];
    size_t i;

    for(i=0; i+1 < sizeof(argv)/sizeof(argv[0]) && compressor[i]; i++) {
	argv[i] = critstrdup(compressor[i],
		"decompresspipe");
    }
    argv[i] = 0;

    if (pipe(feedpipe)) return NULL;
    if (pipe(explodepipe)) return NULL;

    pidfeeder = fork();
    switch (pidfeeder) {
	case -1:
	    return 0;
	case 0:
	    /* child of feeder */
	    pidexploder = fork();
	    switch (pidexploder) {
		case -1:
		    _exit(127);
		case 0:		/* child -- exploder */
		    close(feedpipe[1]);
		    close(explodepipe[0]);
		    if (0 == dup2(feedpipe[0], STDIN_FILENO)) {
			close(feedpipe[0]);
			if (1 == dup2(explodepipe[1], STDOUT_FILENO)) {
			    close(explodepipe[1]);
			    /* leaks argv */
			    execve(compressor[0], argv, NULL);
			    _exit(127);
			}
		    }
		    _exit(1);
		default:		/* parent of exploder */
		    break;
	    }
	    /* feeder, stuff FILE into child */
	    {
		char buf[4096];
		size_t r;
		int rc = 0;
		close(feedpipe[0]);
		close(explodepipe[0]);
		close(explodepipe[1]);

		while(!feof(original) && (r = fread(buf, 1, sizeof(buf), original))) {
		    size_t w = r;
		    while (w) {
			if ((w = write(feedpipe[1], buf, r)) == -1) {
			    rc = 1;
			    break;
			}
			r -= w;
		    }
		}

		if (ferror(original)) {
		    rc = 1;
		}
		close(feedpipe[1]);
		wait(NULL);
		for(i = 0; argv[i]; i++)
		    free(argv[i]);
		_exit(0);
	    }
	    break;
    default:
	; /* parent of feeder */
    }
    close(explodepipe[1]);
    close(feedpipe[0]);
    close(feedpipe[1]);
    f = fdopen(explodepipe[0], "r");
    return f;
}

/*
 * process any file
 * return 0 if failed, 1 otherwise
 */
static int
fprocessfile(FILE * f)
{
    char *l;
    int i;

    const struct compressors {
	const char *command;	/* read from batch */
	const char *compressor[4];/* command to run */
    } compressors[] = {
	{ "cunbatch", { GZIP, "-c", "-d", NULL} },
	{ "gunbatch", { GZIP, "-c", "-d", NULL} },
	{ "zunbatch", { GZIP, "-c", "-d", NULL} },
	{ "bunbatch", { BZIP2, "-c", "-d", NULL} },
    };

    l = getaline(f);
    if (!l || strlen(l) < 2) {
	return 0;
    }
    if (str_isprefix(l, "#! rnews ")) {
	    return fprocessbatch(f, l);
    } else if (str_isprefix(l, "#!")) {
	char *c;

	c = l + 2;
	SKIPLWS(c);
	for (i = 0;
	     i < (long)(sizeof(compressors) / sizeof(struct compressors));
	     i++) {
	    if (str_isprefix(c, compressors[i].command)) {
		FILE *uc;

		uc = decompresspipe(f, compressors[i].compressor);
		if (uc == NULL)
			return 0;
		return fprocessbatch(uc, NULL);
	    }
	}
    }
    return 1;
}

/*
 * process any file
 * return 0 if failed, 1 otherwise
 */
static int
processfile(char *filename)
{
    FILE *f;
    int res;

    if ((f = fopen(filename, "r")) == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unable to open %s: %s\n", filename, strerror(errno));
	return 0;
    }
    res = fprocessfile(f);
    fclose(f);
    return res;
}

static int
processdir(char *pathname)
{
    DIR *dir;
    struct dirent *d;
    char procdir[LN_PATH_MAX];

    if (chdir(pathname) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chdir to %s: %m\n", pathname);
	return -1;
    }
    if (!getcwd(procdir, LN_PATH_MAX)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot getcwd %s: %m\n", procdir);
	return -1;
    }
    if (!(dir = opendir("."))) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m\n", pathname);
	return -1;
    }
    while ((d = readdir(dir))) {
	/* don't process dotfiles */
	if (d->d_name[0] != '.') {
	    (void)processfile(d->d_name);
	    /* storearticle uses chdir! */
	    if (chdir(procdir) < 0) {
		/* Yes, I'm paranoid */
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chdir to %s: %m\n",
		       procdir);
		return -1;
	    }
	}
    }
    closedir(dir);
    return 0;
}

int
main(int argc, char *argv[])
{
    char *ptr;
    char option;
    struct stat st;
    char *conffile = NULL;

    ln_log_open(argv[0]);
    if (!initvars(argv[0], 0))
	exit(EXIT_FAILURE);

    while ((option = getopt(argc, argv, "F:D:Vv")) != -1) {
	if (parseopt("rnews", option, optarg, &conffile))
	    continue;
	switch(option) {
	    default:
		usage();
		exit(EXIT_FAILURE);
	}
    }

    if (readconfig(conffile) != 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Reading configuration failed: %m.\n");
	exit(2);
    }
    if (conffile)
	free(conffile);

    umask((mode_t) 077);

    if (lockfile_exists(5UL)) {
	fprintf(stderr, "%s: lockfile %s exists, abort\n", argv[0], lockfile);
	exit(EXIT_FAILURE);
    }

    if (filterfile && !readfilter(filterfile)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
		"%s: Cannot read filterfile %s, aborting.",
		argv[0], filterfile);
	log_unlink(lockfile, 0);
	exit(EXIT_FAILURE);
    }

    rereadactive();
    readlocalgroups();
    if (!argv[optind])
	fprocessfile(stdin);	/* process stdin */
    while ((ptr = argv[optind++])) {
	if (stat(ptr, &st) == 0) {
	    if (S_ISDIR(st.st_mode))
		processdir(ptr);
	    else if (S_ISREG(st.st_mode))
		processfile(ptr);
	    else
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "%s: cannot open %s\n", argv[0], ptr);
	} else
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "%s: cannot stat %s\n", argv[0], ptr);
    }
    writeactive();		/* write groupinfo file */
    log_unlink(lockfile, 0);
    exit(0);
}
