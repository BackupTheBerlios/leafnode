/** gmtoff.c - get time zone offset in seconds
 * (C) 2001 - 2002 by Matthias Andree
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 or 2.1 of
 * the License. See the file COPYING.LGPL for details.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
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
