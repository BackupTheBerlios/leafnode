/* ripped of miscutil.c */

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
