#include "leafnode.h"
#include <string.h>

/* compare 'KEY' with 'key value' in a symmetric way */
int
cmp_firstcolumn(const void *a, const void *b,
	/*@unused@*/ const void *config __attribute__ ((unused)))
{
    const char *s = a, *t = b;
    size_t m, n;
    static const char WHITE[] = " \t";

    /* slow but safe version */
    m = strcspn(s, WHITE);
    n = strcspn(t, WHITE);

    return strncasecmp(s, t, m > n ? m : n);
}

