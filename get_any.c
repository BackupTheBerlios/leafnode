/* \file get_any.c
   This file wraps strtoul and strtol to simplify error checking.

   \year 2000
   \author Matthias Andree */
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "get.h"

/** Reads a signed long integer from \p in and stores it into the long
 *  that \p var points to. 
 *  \return 
 *  1 for success, 
 *  0 if problem 
 */
int
get_long(const char *in, long *var)
{
    char *e;

    *var = strtol(in, &e, 10);
    if (e == in)
	return 0;
    if ((errno == ERANGE)
	&& ((*var == LONG_MIN) || (*var == LONG_MAX)))
	return 0;
    return 1;
}

/** Reads an unsigned long integer from \p in and stores it into the
 *  unsigned long that \p var points to.  \return 1 for success, 0 if
 *  problem */
int
get_ulong(const char *in, unsigned long *var)
{
    char *e;

    *var = strtoul(in, &e, 10);
    if (e == in)
	return 0;
    if ((errno == ERANGE) && (*var == ULONG_MAX))
	return 0;
    return 1;
}
