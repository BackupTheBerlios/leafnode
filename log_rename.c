/* (C) 2001 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_rename(const char *f, const char *t)
{
    int r = rename(f, t);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot rename %s to %s: %m", f, t);
    return r;
}
