/* (C) 2001 by Matthias Andree */

#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

int
log_fclose(FILE * f)
{
    int r = fclose(f);
    if (r)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot fclose(%p): %m", (void *)f);
    return r;
}
