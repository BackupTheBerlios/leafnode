/* ripped off miscutil.c */

#include "msgid.h"

unsigned int
msgid_hash(const char *name)
{
    int i = 0;
    unsigned int r = 0;
    int stop = 0;

    do {
	if (name[i] == '>')
	    ++stop;
	r += (int)((unsigned char)name[i]);
	r += ++i;
    } while (!stop && name[i]);

    r = r % 999 + 1;
    return r;
}
