/* (C) 2001 - 2002 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_unlink(const char *f, int ignore_enoent)
{
    int r = unlink(f);
    if (r < 0 && errno == ENOENT && ignore_enoent)
	r = 0;
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot unlink %s: %m", f);
    return r;
}
