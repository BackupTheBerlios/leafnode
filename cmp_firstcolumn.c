#include "leafnode.h"
#include <string.h>
#include <ctype.h>

/** compare the first word of two strings ignoring case
 *  'KEY' == 'key value'
 */
int
cmp_firstcolumn(const void *a, const void *b,
        /*@unused@*/ const void *config __attribute__ ((unused)))
{
    const unsigned char *s = a, *t = b;
    unsigned int c, d;

    do {
        c = tolower(*s++);
        d = tolower(*t++);
        if (c == '\0' || c == ' ' || c == '\t') {
	    c = '\0';
            break;
	}
    } while (c == d);
    if (d == ' ' || d == '\t')
	d = '\0';
    return c - d;
}
