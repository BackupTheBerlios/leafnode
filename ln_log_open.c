#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <syslog.h>

void
ln_log_open(const char *ident)
{
#ifndef TESTMODE
#ifdef HAVE_OLD_SYSLOG
    /* support legacy syslog interface */
    openlog(ident, LOG_PID);
#else
    openlog(ident, LOG_PID | LOG_CONS, LOG_NEWS);
#endif
#endif /* not TESTMODE */
}
