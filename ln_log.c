#include "ln_log.h"

#include <stdarg.h>
#include <assert.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

/* add LOG_NEWS where it doesn't exist */
#if !defined(LOG_NEWS)
#define LOG_NEWS LOG_DAEMON
#endif

/* if LOG_CONS isn't supported, do without */
#if !defined(LOG_CONS)
#define LOG_CONS 0 
#endif

static void vln_log_core(int slg, FILE /*@null@*/ *console, int severity, 
		       const char *format, va_list ap);

/* log to syslog if slg != 0
 * log to stream console if console != 0
 * other arguments like syslog */
static void vln_log_core(int slg, FILE *console, int severity, 
		       const char *format, va_list ap)
{
    char buf[2048], fmt[2048],  *y;
    const char *x;
    int errno_save = errno;

    assert(severity >= LNLOG_MIN);

    /* support %m */
    for(x = format, y = fmt; *x && y < fmt + sizeof(fmt) - 2; x++) {
	if (*x == '%') {
	    x++;
	    if (*x == '%') {
		*y++ = *x;
		*y++ = *x;
	    } else if (*x == 'm') {
		char *z = strerror(errno_save);
		while(*z && y < fmt + sizeof(fmt)) {
		    if(*z == '%') *(y++) = *z;
		    *(y++) = *(z++);
		}
	    } else {
		*y++ = '%';
	        *y++ = *x;
	    }
	} else {
	    *y++ = *x;
	}
    }
    *y = '\0';

    vsnprintf(buf, sizeof(buf), fmt, ap);
    if(slg != 0) { 
	syslog(severity, "%s", buf); 
    }
    if(console != NULL) { 
	if(severity - LNLOG_MIN <= verbose) {
	    (void)fputs(buf, console);
	    (void)fputc('\n', console);
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
