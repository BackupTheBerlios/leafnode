#include "ln_log.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <errno.h>
int verbose = 9;

main()
{
    errno = 4;
    ln_log_prt(LNLOG_NOTICE, "test");
    ln_log_prt(LNLOG_NOTICE, "test %%");
    ln_log_prt(LNLOG_NOTICE, "test %d", 4);
    ln_log_prt(LNLOG_NOTICE, "test %m");
}
