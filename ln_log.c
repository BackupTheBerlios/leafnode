#include "ln_log.h"
#include <stdarg.h>
#include <assert.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>		/* for getenv */

#include <signal.h>		/* for raise */
/* add LOG_NEWS where it doesn't exist */
#if !defined(LOG_NEWS)
#define LOG_NEWS LOG_DAEMON
#endif
/* if LOG_CONS isn't supported, do without */
#if !defined(LOG_CONS)
#define LOG_CONS 0
#endif

#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

int ln_log_stderronly;

/* log to syslog if slg != 0
 * log to stream console if console != 0
 * ctx to have verbosity like context
 * other arguments like syslog */
static void
vln_log_core(int slg, FILE /*@null@*/ * console, int severity,
	     int context, const char *format, va_list ap)
{
    char buf[2048], fmt[2048], *y;
    const char *x;
    int errno_save = errno;

    assert(severity >= LNLOG_SMIN);
    assert(context >= 0);
    assert(verbose >= 0);
    /* support %m */
    for (x = format, y = fmt; *x && y < fmt + sizeof(fmt) - 2; x++) {
	if (*x == '%') {
	    x++;
	    if (*x == '%') {
		*y++ = *x;
		*y++ = *x;
	    } else if (*x == 'm') {
		const char *z = strerror(errno_save);

		while (*z && y < fmt + sizeof(fmt)) {
		    if (*z == '%')
			*(y++) = *z;
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
    if (slg != 0 && (severity < LNLOG_SDEBUG || debugmode & DEBUG_LOGGING)) {
	if (ln_log_stderronly) {
	    fprintf(stderr, "%s\n", buf);
	} else {
	    syslog(severity, "%s", buf);
	}
    }

    if (severity <= LNLOG_SERR) {
	/* check if environment demands kill on first error */
	char *k = getenv("LN_LOG_ABORT_ON_ERROR");
	if (k)
	    abort();
    }

    if (console && (!ln_log_stderronly || console != stderr)) {
	/* always log LNLOG_SERR and more severe,
	   regardless of verbosity */
	if ((context <= verbose || severity <= LNLOG_SERR || (severity <= LNLOG_SWARNING && verbose))
		&& (severity < LNLOG_SDEBUG || debugmode & DEBUG_LOGGING)) {
	    (void)fputs(buf, console);
	    (void)fputc('\n', console);
	}
    }
}

void
ln_log(int sev, int ctx, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vln_log_core(1, stderr, sev, ctx, format, ap);
    va_end(ap);
}

void
ln_log_so(int sev, int ctx, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vln_log_core(1, stdout, sev, ctx, format, ap);
    va_end(ap);
}

void
ln_log_prt(int sev, int ctx, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vln_log_core(0, stderr, sev, ctx, format, ap);
    va_end(ap);
}

void
ln_log_sys(int sev, int ctx, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vln_log_core(1, NULL, sev, ctx, format, ap);
    va_end(ap);
}
