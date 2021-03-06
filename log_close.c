/* (C) 2001 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_close(int f)
{
    int r = close(f);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot close(%d): %m", f);
    return r;
}
