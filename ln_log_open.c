#include "ln_log.h"
#include <syslog.h>

void
ln_log_open(const char *ident)
{
#ifdef HAVE_OLD_SYSLOG
    /* support legacy syslog interface */
    openlog(ident, LOG_PID);
#else
    openlog(ident, LOG_PID | LOG_CONS, LOG_NEWS);
#endif
}
