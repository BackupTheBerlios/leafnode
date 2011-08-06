/* (C) 2002 by Matthias Andree */

#define _XOPEN_SOURCE 600

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_fchmod(const int f, mode_t m)
{
    int r = fchmod(f, m);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chmod file descriptor %d to %o: %m", f, (int)m);
    return r;
}
