/*
 * rnews: sort articles or UUCP batchfiles into spool
 *
 * Written and copyrighted by Cornelius Krasel, January-April 1999
 * See README for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "get.h"
#include "critmem.h"
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
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>

#ifdef DEBUG_DMALLOC
#include <dmalloc.h>
#endif

#define TEMPLATE "/tmp/rnews.XXXXXX"
/* extern int optind; */
int debug = 0;
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
fprocessbatch(FILE * f)
{
    char *l;
    long bytes;

    while ((l = getaline(f))) {
	const char tomatch[] = "#! rnews ";

	if (!strisprefix(l, tomatch)) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "expected `#! rnews', got `%-.40s[...]'", l);
	    goto pb_bail;
	}
	if (0 == get_long(l + strlen(tomatch), &bytes)) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "cannot extract article length from `%-.40s[...]'\n", l);
	    goto pb_bail;
	}
	store_stream(f, 0, NULL /* FIXME: filters */ , bytes);
    }
    return 1;
  pb_bail:
    return 0;
}

/* 0: problem
 * FILE pointer: ok
 */
static FILE *
decompresspipe(FILE * original, const char *compressor)
{
    pid_t pidfeeder, pidexploder;
    int stat;

    pidfeeder = fork();
    switch (pidfeeder) {
    case -1:
	return 0;
    case 0:
	pidexploder = fork();
	switch (pidexploder) {
	case -1:
	    _exit(127);
	case 0:		/* child */
	default:		/* parent */
	}
	/* child */
    default:
	/* parent of feeder */
    }
}

/*
 * uncompress a file in filename
 * compressor must support gzip-style arguments and be able run as filter
 * return 1 if successful, 0 if not
 */
static int
old_uncompressfile(const char *filename, const char *compressor)
{
    pid_t pid;
    int statloc;
    char s[PATH_MAX + 1];	/* FIXME */

    pid = fork();
    switch (pid) {
    case 0:
	{
	    int in, out;
	    /* child process uncompresses stuff */
	    snprintf(s, sizeof(s), ".tmp.%s", filename);	/* truncate */

	    in = open(filename, O_RDWR);
	    if (in < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", filename);
		_exit(1);
	    }

	    out = open(s, O_WRONLY | O_CREAT, (mode_t) 0600);
	    if (out < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", filename);
		close(in);
		_exit(1);
	    }

	    if (dup2(in, 0) < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot dup %d: %m", in);
		close(in);
		close(out);
		_exit(1);
	    }

	    if (dup2(out, 1) < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot dup %d: %m", out);
		close(in);
		close(out);
		_exit(1);
	    }

	    execl(compressor, compressor, "-c", "-d", NULL);
	    /* if this is reached, calling gzip has failed */
	    _exit(99);
	}
    case -1:
	ln_log(LNLOG_SERR, LNLOG_CTOP, "unable to fork()\n");
	return 1;
    default:
	break;
    }

    if (waitpid(pid, &statloc, 0) == -1) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Decompressing failed: waitpid returned error: %s\n",
	       strerror(errno));
    }

    if (WIFEXITED(statloc) == 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "Decompressing failed: child crashed.\n");
	log_unlink(s);
	return 0;
    } else if (WEXITSTATUS(statloc)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Decompressing failed, got %d.\n",
	       WEXITSTATUS(statloc));
	log_unlink(s);
	return 0;
    }
    return log_rename(s, filename) ? 0 : 1;
}

/*
 * process any file
 * return 0 if failed, 1 otherwise
 */
static int
fprocessfile(FILE * f)
{
    char *l;
    const char *tempfilename = 0;
    int i;

    const struct compressors {
	const char *command;	/* read from batch */
	const char *compressor;	/* command to run */
    } compressors[] = {
	{
	"cunbatch", GZIP}, {
	"rnews", CAT}, {
	"zunbatch", GZIP}
    };

#if 0
    debug = 0;
    l = getaline(f);
    debug = debugmode;
    if (!l || strlen(l) < 2) {
	return 0;
    }
    if (strisprefix(l, "#!")) {
	char *c;

	c = l + 2;
	SKIPLWS(c);
	for (i = 0;
	     i < (long)(sizeof(compressors) / sizeof(struct compressors));
	     i++) {
	    if (strisprefix(c, compressors[i].command)) {
		int j;
		/* FIXME: fuck me */
/*		tempfilename = copytotempfile(f); */
		if (tempfilename) {
		    if (!uncompressfile(tempfilename,
					compressors[i].compressor)) {
			return 0;
		    }
		    j = processbatch(tempfilename);
		    if (j)
			log_unlink(tempfilename);
		} else {
		    j = 0;
		}
		return j;
	    }
	}
    }
    return 1;
#else
    return 0;
#endif
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
    char *procdir;

    if (chdir(pathname) < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chdir to %s: %s\n", pathname,
	       strerror(errno));
	return -1;
    }
    procdir = getcwd(NULL, 0);
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
    free(procdir);
    return 0;
}

int
main(int argc, char *argv[])
{
    char *ptr;
    char option;
    struct stat st;

    puts("this program cannot currently be used.");
    abort();

    if (!initvars(argv[0]))
	exit(EXIT_FAILURE);
    ln_log_open(argv[0]);

    while ((option = getopt(argc, argv, "DVv")) != -1) {
	if (!parseopt("rnews", option, NULL, NULL, 0)) {
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    debug = debugmode;
    umask((mode_t) 077);

    whoami();
    if (lockfile_exists(FALSE))
	exit(EXIT_FAILURE);
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
    fixxover();			/* fix xoverview files */
    log_unlink(lockfile);
    exit(0);
}
