#include <string.h>

#include "fetchnews.h"
#include "system.h"
#include "leafnode.h"
#include "ln_log.h"

/* send DATE command to upstream server and complain if the clocks are
 * more than 15 minutes apart.
 */
void
check_date(const struct serverlist *server)
{
    int reply;
    struct tm tm;
    time_t t, to;
    const int tolerate = 10;
    const int debugmask = DEBUG_NNTP|DEBUG_LOGGING;
    char *lastline;

    putaline(nntpout, "DATE");
    reply = newnntpreply(server, &lastline);
    if (reply != 111) {
	/* upstream does not support the DATE command, so ignore */
	if ((debugmode & debugmask) == debugmask) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CSERVER,
		    "check_date: %s does not support DATE, "
		    "reply %d, expected 111", server->name, reply);
	}
	return;
    }

    /* upstream supports the DATE command */
    if (sscanf(lastline, "%*d %4d%2d%2d%2d%2d%2d",
		&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		&tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 6) {
	/* too few fields */
	if ((debugmode & debugmask) == debugmask) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CSERVER, "check_date: %s: too few fields in DATE reply "
		    "\"%s\"", server->name, lastline);
	}
	return;
    }

    /* we can match 6 fields, parse date */
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1; /* let libc figure time zone offset */
    t = mktime(&tm);
    if (t == (time_t) - 1) {
	/* error, ignore */
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "check_date: %s: upstream sends unparsable reply "
		"to DATE, mktime failed. \"%s\"", server->name, lastline);
	return;
    }

    /* mktime assumes local time zone, compensate */
    t += gmtoff(t);

    if (labs(t - time(&to)) > tolerate * 60) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "check_date: %s: clocks of upstream and this computer are more than %d minutes apart. Check your system clock.", server->name, tolerate);
    } else {
	if ((debugmode & debugmask) == debugmask) {
	ln_log(LNLOG_SDEBUG, LNLOG_CSERVER, "check_date: %s: server time %ld, our time %ld",
		server->name, t, to);
	}
    }
}


