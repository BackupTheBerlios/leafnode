#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <syslog.h>

void
ln_log_open(const char *ident)
{
    openlog(ident, LOG_PID | LOG_CONS, LOG_NEWS);
}
