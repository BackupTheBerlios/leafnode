/*
 * stccpy.c
 *
 * (C) 2001 by Matthias Andree
 */


#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "mastring.h"

/* this function is a strcpy reimplementation that returns a pointer 
 * to the NUL byte in the destination string, thus, there's no need for
 * subsequent implicit strlen traversals for strcat */
char *
mastrcpy(/*@out@*/ /*@returned@*/ char *dest, const char *src)
{
    while (*src) {
	*(dest++) = *(src++);
    }
    *dest = '\0';
    return dest;
}
