#include <string.h>
#include <ctype.h>

#include "fetchnews.h"
#include "system.h"
#include "leafnode.h"
#include "ln_log.h"

static int
get_nr(char *s, int count, int *dst)
{
    int i;

    *dst = 0;

    for (i = 0; i < count; i++) {
	if (!isdigit((unsigned char)s[i])) return FALSE;
	*dst *= 10;
	*dst += s[i] - '0';
    }

    return TRUE;
}

static int
scan_date(char *l, struct tm *tm)
{
    SKIPWORD(l);
    if (strlen(l) != 14) return FALSE;

    return get_nr(l,    4, &tm->tm_year) &&
    	   get_nr(l+4,  2, &tm->tm_mon) &&
	   get_nr(l+6,  2, &tm->tm_mday) &&
	   get_nr(l+8,  2, &tm->tm_hour) &&
	   get_nr(l+10, 2, &tm->tm_min) &&
	   get_nr(l+12, 2, &tm->tm_sec);
}

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
    char *lastline, *tz;
    const char *const tzvar = "TZ";

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
    if (!scan_date(lastline, &tm)) {
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

    /* mktime assumes local time, so let's sell it a different idea */
    tz = getenv(tzvar);
    setenv(tzvar, "UTC", 1);
    tzset();
    t = mktime(&tm);
    if (tz)
        setenv(tzvar, tz, 1);
    else
        unsetenv(tzvar);
    tzset();

    if (t == (time_t) - 1) {
	/* error, ignore */
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "check_date: %s: upstream sends unparsable reply "
		"to DATE, mktime failed. \"%s\"", server->name, lastline);
	return;
    }

    if (labs(t - time(&to)) > tolerate * 60) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "check_date: %s: clocks of upstream and this computer are more than %d minutes apart. Check your system clock.", server->name, tolerate);
    } else {
	if ((debugmode & debugmask) == debugmask) {
	ln_log(LNLOG_SDEBUG, LNLOG_CSERVER, "check_date: %s: server time %ld, our time %ld",
		server->name, (long int)t, (long int)to);
	}
    }
}
