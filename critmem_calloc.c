#include "config.h"
#include "critmem.h"
#include "attributes.h"
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
mycritcalloc(const char *f, long l, size_t size, const char *message)
{
    void *a;

#ifdef WITH_DMALLOC
    a = _calloc_leap(f, l, 1, size);
#else
    (void)f;
    (void)l;			/* shut up compiler warnings */
    a = calloc(1, size);
    if (a == NULL && size == 0)
	a = calloc(1,1);
#endif

    if (!a) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "calloc(1, %d) failed: %s", (int)size, message);
	exit(EXIT_FAILURE);
    }
    return a;
}
