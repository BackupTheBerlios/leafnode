#include "leafnode.h"
#include "ln_log.h"

#include <signal.h>
#include <setjmp.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <unistd.h>

static jmp_buf to;
static void
timer(int sig)
{
    (void)sig;
    longjmp(to, 1);
}

/*
 * call getaline with timeout
 */
char *
timeout_getaline(FILE * f, int timeout)
{
    char *l;

    if (setjmp(to)) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "timeout reading.");
	return NULL;
    }
    (void)signal(SIGALRM, timer);
    (void)alarm(timeout);
    l = getaline(f);
    (void)alarm(0U);
    return l;
}

char *
mgetaline(FILE * f)
{
    return timeout_getaline(f, 300);
}
