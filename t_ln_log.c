#include "ln_log.h"
#include <errno.h>

int verbose=9;

main() {
    errno = 4; 
    ln_log_prt(LNLOG_NOTICE, "test");
    ln_log_prt(LNLOG_NOTICE, "test %%");
    ln_log_prt(LNLOG_NOTICE, "test %d", 4);
    ln_log_prt(LNLOG_NOTICE, "test %m");
}
