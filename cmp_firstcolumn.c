#include "leafnode.h"
#include <string.h>
#include <ctype.h>

/** compare the first word of two strings ignoring case
 *  'KEY' == 'key value'
 *
 *  word is the longest string from the beginning to the first TAB or
 *  SPACE. The delimiter is not compared.
 */
int
cmp_firstcolumn(const void *a, const void *b,
        /*@unused@*/ const void *config __attribute__ ((unused)))
{
    const unsigned char *s = (const unsigned char*)a;
    const unsigned char *t = (const unsigned char*)b;
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
