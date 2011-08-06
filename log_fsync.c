/* (C) 2001 by Matthias Andree */

#define _XOPEN_SOURCE 600
#include <unistd.h>
#include <stdio.h>
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
