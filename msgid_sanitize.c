/* ripped of miscutil.c */


#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "msgid.h"

void
msgid_sanitize(char *m)
{
    while (*m) {
	if (*m == '/')
	    *m = '@';
	if (*m == '>')
	    m[1] = '\0';
	m++;
    }
}
