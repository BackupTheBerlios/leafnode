
#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "format.h"

void
str_ulong( /*@out@*/ char *p, unsigned long u)
{
    unsigned int len = 0;
    unsigned long i = u;

    unsigned char b[22];

    do {
	b[len++] = i % 10;
	i /= 10;
    } while (i);

    while (len) {
	*p++ = "0123456789"[b[--len]];
    }
    *p = '\0';
}
