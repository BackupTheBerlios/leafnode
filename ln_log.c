#include "ln_log.h"

#include <stdarg.h>
#include <assert.h>
#include <syslog.h>
#include <stdio.h>

/* add LOG_NEWS where it doesn't exist */
#if !defined(LOG_NEWS)
#define LOG_NEWS LOG_DAEMON
#endif

/* if LOG_CONS isn't supported, do without */
#if !defined(LOG_CONS)
#define LOG_CONS 0 
#endif

/* log to syslog if slg != 0
 * log to stream console if console != 0
 * other arguments like syslog, except it does not support %m */
static void vln_log_core(int slg, FILE *console, int severity, 
		       const char *format, va_list ap)
{
    char buf[2048];

    assert(severity >= LNLOG_MIN);

    vsnprintf(buf, sizeof(buf), format, ap);
    if(slg) { 
	syslog(severity, "%s", buf); 
    }
    if(console) { 
	if(severity - LNLOG_MIN <= verbose) {
	    fputs(buf, console);
	    fputc('\n', console);
	}
    }
}

void ln_log(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(1, stderr, sev, format, ap); va_end(ap);
}

void ln_log_so(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(1, stdout, sev, format, ap); va_end(ap);
}

void ln_log_prt(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(0, stderr, sev, format, ap); va_end(ap);
}

void ln_log_sys(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(1, 0, sev, format, ap); va_end(ap);
}
