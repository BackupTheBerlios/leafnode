/* (C) 2002 by Matthias Andree */

#include <sys/types.h>
#include <sys/stat.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_chmod(const char *f, mode_t m)
{
    int r = chmod(f, m);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chmod %s to %o: %m", f, (int)m);
    return r;
}
