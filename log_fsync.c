/* (C) 2001 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_fsync(int fd)
{
    int r = fsync(fd);
    if (r < 0)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot fsync(%d): %m", fd);
    return r;
}
