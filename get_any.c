/* get_any.c
   (C) 2000 by Matthias Andree */

#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "get.h"

int get_long(const char *in, long *var) {
    char *e;
    *var = strtol(in, &e, 10);
    if(e == in) return 0;
    if((errno == ERANGE)
       && ((*var == LONG_MIN) || (*var == LONG_MAX))) return 0;
    return 1;
}

int get_ulong(const char *in, unsigned long *var) {
    char *e;
    *var = strtoul(in, &e, 10);
    if(e == in) return 0;
    if((errno == ERANGE) && (*var == ULONG_MAX)) return 0;
    return 1;
}
