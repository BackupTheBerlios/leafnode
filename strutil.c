/* strutil.c
  
   (C) 2000 by Matthias Andree
   GNU GPL 2
*/

#include <string.h>
#include "leafnode.h"
#include "critmem.h"

int check_allnum_minus(const char *x) {
    while(*x) {
	if(!strchr("-0123456789", *x)) return 0;
	x++;
    }
    return 1;
}

char *cuttab(const char *in, int field)
{
    const char *j;
    char *k;
    while(--field > 0 && *in) {
	while(*in && *in != '\t') in++;
	in++; /* skip tab */
    }
    if(!*in) return 0;

    j=in;
    while(*in && *in != '\t') in++;
    k=critmalloc(in-j+1, "cuttab");
    strncpy(k, j, in-j);
    k[in-j]=0;
    return k;
}
