#ifndef LNLOG_H
#define LNLOG_H

#include "attributes.h"
#include <stdarg.h>

/* severities */
/* in analogy to syslog levels, are passed on to syslog */
#if 0
#define	LNLOG_SEMERG	0	/*   system is unusable */
#define	LNLOG_SALERT	1	/*   action must be taken immediately */
#endif
#define	LNLOG_SCRIT	2	/*   critical conditions */
#define	LNLOG_SERR	3	/* * error conditions */
#define	LNLOG_SWARNING	4	/* * warning conditions */
#define	LNLOG_SNOTICE	5	/* * normal but significant condition */
#define	LNLOG_SINFO	6	/* * informational */
#define	LNLOG_SDEBUG	7	/* * debug-level messages */
#define LNLOG_SMIN      2	/* minimal used severity */
/* contexts */
/* define the context the log message occurs in
   think of it as "verbose level" */
#define LNLOG_CTOP        0	/* top level, always log */
#define LNLOG_CSERVER     1	/* server context */
#define LNLOG_CGROUP      2	/* group context */
#define LNLOG_CARTICLE    3	/* article context */
#define LNLOG_CALL	  4	/* most verbose */

/* EXPORT */
extern void ln_log_open(const char *ident);

/* log to stderr and syslog */
extern void ln_log(int severity, int context, const char *format, ...)
    __attribute__ ((format(printf, 3, 4)));

/* log to stdout and syslog */
extern void ln_log_so(int severity, int context, const char *format, ...)
    __attribute__ ((format(printf, 3, 4)));

/* log to stderr only */
extern void ln_log_prt(int severity, int context, const char *format, ...)
    __attribute__ ((format(printf, 3, 4)));

/* log to syslog only */
extern void ln_log_sys(int severity, int context, const char *format, ...)
    __attribute__ ((format(printf, 3, 4)));
#endif
