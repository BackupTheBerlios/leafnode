#include "leafnode.h"
#include "ln_log.h"

#include <signal.h>
#include <setjmp.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

static jmp_buf to;
static RETSIGTYPE
timer(int sig)
{
    (void)sig;
    siglongjmp(to, 1);
}

/*
 * call getaline with timeout
 */
char *
timeout_getaline(FILE * f, unsigned int timeout)
{
    char *l;
    struct sigaction sa;

    if (sigsetjmp(to, 1)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "timeout reading.");
	return NULL;
    }
    sa.sa_handler = timer;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGALRM, &sa, NULL);
    (void)alarm(timeout);
    l = getaline(f);
    (void)alarm(0U);
    return l;
}

char *
mgetaline(FILE * f)
{
    return timeout_getaline(f, timeout_client);
}
