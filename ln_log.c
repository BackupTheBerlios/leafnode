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

static void vln_log_core(int slg, int console, int severity, 
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
	    fputs(buf, stderr);
	    fputc('\n', stderr);
	}
    }
}

void ln_log(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(1, 1, sev, format, ap); va_end(ap);
}

void ln_log_prt(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(0, 1, sev, format, ap); va_end(ap);
}

void ln_log_sys(int sev, const char *format, ...) {
    va_list ap;
    va_start(ap, format); vln_log_core(1, 0, sev, format, ap); va_end(ap);
}
