/** gmtoff.c - get time zone offset in seconds
 * (C) 2001 by Matthias Andree
 */

#include "leafnode.h"


#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <time.h>

/** Returns number of seconds the local time zone is ahead (east) of UTC */
time_t
gmtoff(void)
{
    time_t t1, t2;
    struct tm *t;

    t1 = time(0);
    t = gmtime(&t1);
    t2 = mktime(t);

    return t1 - t2;
}
