/** \file strcasestr.c -- strcasestr replacement 
 * \author Matthias Andree
 * \year 2004
 *
 * GNU LESSER PUBLIC LICENSE, v2.1
 */

#include "system.h"

#include <stdlib.h>
#include <string.h>

char *strcasestr(const char *haystack, const char *needle)
{
    size_t i, n = strlen(needle);
    if (n > strlen(haystack)) return NULL;
    i = strlen(haystack) - n;
    for(;;) {
	if (strncasecmp(haystack, needle, n) == 0)
	    return haystack;
	if (i == 0)
	    return NULL;
	haystack++;
	i--;
    }
}
