#include "pcrewrap.h"
#include <pcre.h>
#include "ln_log.h"

pcre *ln_pcre_compile(const char *pattern, int options,
	const unsigned char *tableptr, const char *filename,
	unsigned long line)
{
    const char *regex_errmsg;
    int regex_errpos;
    pcre *re;

    re = pcre_compile(pattern, options,
	    &regex_errmsg, &regex_errpos, tableptr);
    if (!re) {
	/* could not compile */
	ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		"%s: invalid pattern at line %lu: %s",
		filename, line, regex_errmsg);
	ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		"%s: \"%s\"", filename, pattern);
	ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		"%s:  %*s",
		filename,
		regex_errpos + 1, "^");
    }
    return re;
}
