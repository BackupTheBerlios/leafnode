#ifndef LNLOG_H
#define LNLOG_H

#include "attributes.h"
#include <stdarg.h>
/* #include <syslog.h> */

/* severities */
#if 0
#define	LNLOG_EMERG	0	/*   system is unusable */
#define	LNLOG_ALERT	1	/*   action must be taken immediately */
#define	LNLOG_CRIT	2	/*   critical conditions */
#endif
#define	LNLOG_ERR	3	/* * error conditions */
#define	LNLOG_WARNING	4	/* * warning conditions */
#define	LNLOG_NOTICE	5	/* * normal but significant condition */
#define	LNLOG_INFO	6	/* * informational */
#define	LNLOG_DEBUG	7	/* * debug-level messages */

#define LNLOG_MIN     3       /* minimal used severity */

/* IMPORT */
extern int verbose;

/* EXPORT */
extern void ln_log_open (const char *ident);

extern void ln_log(int severity, const char *format, ...)
    __attribute__ ((format (printf, 2, 3)));

extern void ln_log_prt(int severity, const char *format, ...)
    __attribute__ ((format (printf, 2, 3)));

extern void ln_log_sys(int severity, const char *format, ...)
    __attribute__ ((format (printf, 2, 3)));

#endif
