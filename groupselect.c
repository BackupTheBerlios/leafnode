#include <string.h>
#include <pcre.h>

#include "config.h"
#include "groupselect.h"
#include "ln_log.h"

pcre *gs_compile(const char *regex) {
    const char *regex_errmsg;
    int regex_errpos;
    pcre *e;

    if ((e = pcre_compile(regex, PCRE_MULTILINE, &regex_errmsg, &regex_errpos
#ifdef NEW_PCRE_COMPILE
		    , NULL
#endif
		    )) == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "Invalid group pattern "
		"in \"%s\" at char #%d: %s", regex, regex_errpos, regex_errmsg);
    }
    return e;
}

/* match s against PCRE p
 * WARNING: If p is NULL, every string s is considered a match
 * returns 1 for match, 0 for mismatch, negative for error */
int gs_match(const pcre *p, const char *s) {
    int match;
    if (p == NULL) return 1;
    match = pcre_exec(p, NULL, s, strlen(s),
#ifdef NEW_PCRE_EXEC
	    0,
#endif
	    PCRE_ANCHORED, NULL, 0);

    if (match == PCRE_ERROR_NOMATCH) return 0;
    if (match >= 0) return 1;
    return match;
}
