#include "config.h"
#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/*
 * replacement for realloc, logs allocation failures
 * and exits with the error message
 */
void *
mycritrealloc(const char *f, long l, void *oa, size_t size, const char *message)
{

#ifdef WITH_DMALLOC
    a = _realloc_leap(f, l, oa, size);
#else
    (void)f;
    (void)l;			/* shut up compiler warnings */
    a = realloc(oa, size);
    if (a == NULL && size == 0)
	a = realloc(oa, 1);
#endif
    if (!a) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "realloc(%d) failed: %s", (int)size, message);
	exit(EXIT_FAILURE);
    }
    return a;
}
