#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/*
 * replacement for malloc, logs allocation failures
 * and exits with the error message
 */
void *
mycritmalloc(const char *f, long l, size_t size, const char *message)
{
    void *a;

#ifdef WITH_DMALLOC
    a = _malloc_leap(f, l, size);
#else
    (void)f;
    (void)l;			/* shut up compiler warnings */
    a = malloc(size);
#endif
    if (!a) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "malloc(%d) failed: %s", (int)size, message);
	exit(1);
    }
    return a;
}
