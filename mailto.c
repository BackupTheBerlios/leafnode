/** mailto.c - send mail from a file descriptor
 * (C) 2001 - 2002 by Matthias Andree
 */

#include "leafnode.h"
#include "ln_log.h"
#include "mailto.h"

#include <sysexits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

static void log_status(int status) {
    if (WIFSIGNALED(status)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
		"mailto: program %s died of unhandled signal %d",
		mta, WTERMSIG(status));
    } else if (WIFEXITED(status)) {
	ln_log(WEXITSTATUS(status) ? LNLOG_SERR : LNLOG_SDEBUG,
		LNLOG_CTOP,
		"mailto: program %s exited with status %d",
		mta, WEXITSTATUS(status));
    } else {
	ln_log(LNLOG_SERR, LNLOG_CTOP, 
		"mailto: cannot make sense of wait status %d", status);
    }
}

/** mailto - mail a file to address. Expects fd to be a readable file
 * descriptor. \return 0 for success, negative integer for error
 * WARNING: This function expects that SIGCHLD is _NOT_ set to SIG_IGN,
 * it will mail away the file but report it failed otherwise!
 */
int
mailto(const char *address, int fd)
{
    int status, rc;
    int pfd[2];
    int mailpipe[2];
    char buf[1024];
    pid_t pid;
    FILE *in, *out;
    struct sigaction oldaction;
    struct sigaction newaction;

    in = fdopen(fd, "r");
    if (in == NULL)
	return -1;

    if (pipe(pfd))
	return -2;

    if (pipe(mailpipe)) {
	close(pfd[0]);
	close(pfd[1]);
	return -3;
    }

    out = fdopen(mailpipe[1], "w");
    if (out == NULL) {
	close(pfd[0]);
	close(pfd[1]);
	close(mailpipe[0]);
	close(mailpipe[1]);
	return -4;
    }

    newaction.sa_handler=SIG_IGN;
    newaction.sa_flags=SA_RESTART;
    sigemptyset(&newaction.sa_mask);
    sigaction(SIGPIPE, &newaction, &oldaction);

    pid = fork();
    if (pid < 0) {
	close(mailpipe[0]);
	close(mailpipe[1]);
	close(pfd[0]);
	close(pfd[1]);
	return -5;
    }

    switch (pid) {
    case 0:			/* child */
	{
	    if (dup2(mailpipe[0], 0) < 0)
		goto err;
	    close(mailpipe[0]);
	    close(mailpipe[1]);
	    if (dup2(pfd[1], 1) < 0)
		goto err;
	    if (dup2(pfd[1], 2) < 0)
		goto err;
	    close(pfd[0]);
	    close(pfd[1]);
	    execl(mta, mta, "-oi", address, NULL);
	    _exit(EX_OSERR);
	  err:
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "mailto: cannot dup2: %m");
	    _exit(EX_OSERR);
	}
    default:			/* parent */
	{
	    ssize_t s;
	    char *l;
	    int haveto = 0;

	    log_close(pfd[1]);
	    log_close(mailpipe[0]);

	    /* copy news from fd to pipe */
	    /* - header */
	    while((l = getaline(in)) && *l) {
		if (str_isprefix(l, "To:")) haveto = 1;
		if (fputs(l, out) == EOF) goto killmta;
		if (fputs("\n", out) == EOF) goto killmta;
	    }
	    if (ferror(in)) goto killmta;
	    if (!haveto) {
		if (fputs("To: ", out) == EOF) goto killmta;
		if (fputs(address, out) == EOF) goto killmta;
		if (fputs("\n", out) == EOF) goto killmta;
	    }
	    if (fputs("\n", out) == EOF) goto killmta;
	    /* - body */
	    while((l = getaline(in))) {
		if (fputs(l, out) == EOF) goto killmta;
		if (fputs("\n", out) == EOF) goto killmta;
	    }
	    if (fflush(out)) goto killmta;
	    if (ferror(in)) goto killmta;
	    goto pipe_ok;
killmta:
	    (void)usleep(1); /* schedule to let the MTA exit */
	    if (waitpid(pid, &status, WNOHANG) <= 0) {
		ln_log(LNLOG_SERR, LNLOG_CARTICLE, "error while mailing article, killing MTA %s", mta);
		kill(pid, SIGTERM);
		sleep(5);
		if (waitpid(pid, NULL, WNOHANG) != pid) {
		    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "MTA did not respond to SIGTERM, sending SIGKILL");
		    kill (pid, SIGKILL);
		    waitpid(pid, &status, 0); /* reap zombie */
		}
	    } else {
		/* MTA died */
		ln_log(LNLOG_SERR, LNLOG_CARTICLE, "MTA %s exited prematurely", mta);
		log_status(status);
	    }
	    rc = -6;
	    goto bye;

pipe_ok:
	    if (fclose(out)) goto killmta;
	    while ((s = read(pfd[0], buf, sizeof(buf) - 1)) > 0) {
		buf[s] = '\0';
		ln_log(LNLOG_SNOTICE, LNLOG_CTOP, "mailto: output of mta: %s",
		       buf);
	    }

	    if (waitpid(pid, &status, 0) < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "mailto: waitpid error: %m");
		rc = -7; goto bye;
	    }

	    if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status))) {
		log_status(status);
		rc = -8; goto bye;
	    }
	    rc = 0;
	    goto bye;
	}
    }
bye:
	    log_close(pfd[0]);
	    sigaction(SIGPIPE, &oldaction, NULL);
	    return rc;
}

#ifdef TEST
#include <fcntl.h>

int main(int argc, char **argv) {
    int fd, rc;

    ln_log_open("mailto");

    if (argc != 4) {
	fprintf(stderr, "Usage: %s file mta address\n", argv[0]);
	exit(2);
    }

    if ((fd = open(argv[1], O_RDONLY)) < 0) {
	perror("open");
	exit(1);
    }

    mta = argv[2];
    rc = mailto(argv[3], fd);
    fprintf(stderr, "mailto(%s,%d) returned %d\n", argv[3], fd, rc);
    return rc != 0;
}
#endif
