#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "ln_log.h"

#ifdef DEBUG_DMALLOC
#include <dmalloc.h>
#endif

/*
 * replacement for realloc, logs allocation failures
 * and exits with the error message
 */
void *
mycritrealloc(const char *f, long l, void *a, size_t size, const char *message)
{

#ifdef DEBUG_DMALLOC
    a = _realloc_leap(f, l, a, size);
#else
    (void)f;
    (void)l;			/* shut up compiler warnings */
    a = realloc(a, size);
#endif
    if (!a) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "realloc(%d) failed: %s", (int)size, message);
	exit(1);
    }
    return a;
}
