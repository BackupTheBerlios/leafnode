
#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "format.h"

void
str_ulong0(char *p, unsigned long u, unsigned int len)
{
    p[len] = '\0';
    while (len) {
	p[--len] = "0123456789"[u % 10];
	u /= 10;
    }
}
