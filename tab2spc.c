/* tab2spc.c
   (C) 2001 by Matthias Andree */

#include "leafnode.h"

void
tab2spc(char *s)
{
    while (*s) {
	if (*s == '\t')
	    *s = ' ';
	s++;
    }
}
