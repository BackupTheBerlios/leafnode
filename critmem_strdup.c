#include "config.h"
#include "critmem.h"
#include <string.h>
#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/*
 * replacement for malloc, logs allocation failures
 * and exits with the error message
 */
char *
mycritstrdup(const char *f, long l, const char *source, const char *message)
{
    char *a;

    a = (char *)mycritmalloc(f, l, strlen(source) + 1, message);
    strcpy(a, source);		/* Flawfinder: ignore -- this is allocated above */
    return a;
}
