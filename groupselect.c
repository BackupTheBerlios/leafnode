#include <string.h>
#include <pcre.h>

#include "config.h"
#include "groupselect.h"
#include "ln_log.h"
#include "pcrewrap.h"

pcre *gs_compile(const char *regex, const char *file, unsigned long line) {
    return ln_pcre_compile(regex, PCRE_MULTILINE, NULL, file, line);
}

/* match s against PCRE p
 * WARNING: If p is NULL, every string s is considered a match
 * returns 1 for match, 0 for mismatch, negative for error */
int gs_match(const pcre *p, const char *s) {
    int match;
    if (p == NULL) return 1;
    match = pcre_exec(p, NULL, s, strlen(s), 0, PCRE_ANCHORED, NULL, 0);

    if (match == PCRE_ERROR_NOMATCH) return 0;
    if (match >= 0) return 1;
    return match;
}
