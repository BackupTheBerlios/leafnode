/*
 * mastrncpy.c
 *
 * (C) 2001 by Matthias Andree
 */

#include "mastring.h"

/* this function is a strncpy reimplementation that always NUL
 * terminates the destination string, but there is no padding as in
 * strncpy. If there is space left in the destination string, returns a
 * pointer to the NUL byte. It returns 0 if there is no space left. */
/*@null@*/ char *
mastrncpy(/*@out@*/ /*@unique@*/ /*@returned@*/ char *dest, const char *src, size_t n)
{
    if (!n)
	return 0;

    while (*src && n > 0) {
	*dest++ = *src++;
	--n;
    }

    /* storage space exhausted */
    if (n < 1) {
	--dest;
	*dest = 0;
	return 0;
    }

    *dest = 0;
    return n == 1 ? 0 : dest;
}
