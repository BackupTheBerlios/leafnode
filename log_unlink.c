/* (C) 2001 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"
#include "ln_log.h"

int
log_unlink(const char *f)
{
    int r = unlink(f);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot unlink %s: %m", f);
    return r;
}
