/* (C) 2002 by Matthias Andree */

#include <sys/types.h>
#include <sys/stat.h>
#include "leafnode.h"
#include "ln_log.h"

int
log_fchmod(const int f, mode_t m)
{
    int r = fchmod(f, m);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot chmod %d to %o: %m", f, (int)m);
    return r;
}
