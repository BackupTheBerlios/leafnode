/** mailto.c - send mail from a file descriptor
 * (C) 2001 by Matthias Andree
 */

#include "leafnode.h"
#include "ln_log.h"

#include <sysexits.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

/** mailto - mail a file to address. Expects fd to be a readable file
 * descriptor. \return 0 for success, -1 for error
 * WARNING: This function expects that SIGCHLD is _NOT_ set to SIG_IGN,
 * it will mail away the file but report it failed otherwise!
 */
int
mailto(const char *address, int fd)
{
    int status;
    int pfd[2];
    char buf[1024];
    pid_t pid;

    if (pipe(pfd))
	return -1;

    pid = fork();
    if (pid < 0) {
	close(pfd[0]);
	close(pfd[1]);
	return -1;
    }

    switch (pid) {
    case 0:			/* child */
	{
	    log_close(pfd[0]);
	    if (dup2(fd, 0) < 0)
		goto err;
	    log_close(fd);
	    if (dup2(pfd[1], 1) < 0)
		goto err;
	    if (dup2(pfd[1], 2) < 0)
		goto err;
	    log_close(pfd[1]);
	    execl(mta, mta, "-oi", address, 0);
	    _exit(EX_OSERR);
	  err:
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "mailto: cannot dup2: %m");
	    _exit(EX_OSERR);
	}
    default:			/* parent */
	{
	    ssize_t s;
	    (void)close(pfd[1]);

	    while ((s = read(pfd[0], buf, sizeof(buf) - 1)) > 0) {
		buf[s] = '\0';
		ln_log(LNLOG_SNOTICE, LNLOG_CTOP, "mailto: output of mta: %s",
		       buf);
	    }
	    if (wait(&status) < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "mailto: wait error: %m");
		return -1;
	    }

	    if (WIFSIGNALED(status)) {
		ln_log(LNLOG_SERR, LNLOG_CTOP,
		       "mailto: program %s died of unhandled signal %d",
		       mta, WTERMSIG(status));
	    } else if (WIFEXITED(status)) {
		ln_log(WEXITSTATUS(status) ? LNLOG_SERR : LNLOG_SDEBUG,
		       LNLOG_CTOP,
		       "mailto: program %s exited with status %d",
		       mta, WEXITSTATUS(status));
	    }
	    return 0;
	}
    }
}
