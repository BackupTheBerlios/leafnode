/** gmtoff.c - get time zone offset in seconds
 * (C) 2001 - 2002 by Matthias Andree
 */

#include "leafnode.h"


#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <time.h>

/** Returns number of seconds the local time zone is ahead (east) of
 * UTC, for the given time. */
time_t
gmtoff(const time_t t1)
{
    time_t t2;
    struct tm *t;

    t = gmtime(&t1);
    t2 = mktime(t);

    return t1 - t2;
}
