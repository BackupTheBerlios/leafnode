/** \file strutil.c
 *  various string manipulation functions
 *
 * \date 2000 - 2001 
 * \author Matthias Andree
 *
 * \bug Doxygen documentation tags are incomplete.
 * \bug License is missing.
 *
 * (C) 2000 - 2001 by Matthias Andree
 * GNU LGPL 2
 */

#include <string.h>
#include "leafnode.h"
#include "critmem.h"
#include "mastring.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

int
check_allnum_minus(const char *x)
{
    while (*x) {
	if (!strchr("-0123456789", *x))
	    return 0;
	x++;
    }
    return 1;
}

int
check_allnum(const char *x)
{
    while (*x) {
	if (!strchr("0123456789", *x))
	    return 0;
	x++;
    }
    return 1;
}

/*@null@*/ /*@only@*/ char *
cuttab(const char *in, int field)
{
    const char *j;
    char *k;

    while (--field > 0 && *in) {
	while (*in && *in != '\t')
	    in++;
	in++;			/* skip tab */
    }
    if (!*in)
	return NULL;

    j = in;
    while (*in && *in != '\t')
	in++;
    k = (char *)critmalloc(in - j + 1, "cuttab");
    strncpy(k, j, in - j);
    k[in - j] = '\0';
    return k;
}

/** Split string into newly allocated array of up to maxelem substrings.
   The individual strings and the array itself must be freed by free()
   The last array element is always the 0 pointer.

   \return
     number of elements, negated if there was a remainder in the string.
*/
int
str_nsplit(/*@out@*/ char **a /** pre-allocated destination array to hold substrings */ ,
	  const char *const in /* input string to split */ ,
	  const char *sep
		    /** string of delimiter characters, but \b note:
                        only one character is skipped, much unlike
                        strtok or strspn.  */ ,
	  int maxelem /** number of elements that \p a can hold */ )
{
    char **b = a;
    const char *x = in;
    int i = 0;
    int trail = 1;

    if (maxelem <= 0)
	return 0;

    while (i + 1 < maxelem && (*x || trail)) {
	size_t len = strcspn(x, sep);
	trail = 0;
	*b = (char *)critmalloc(len + 1, "strnsplit");
	(void)mastrncpy(*b, x, len + 1);
	x += len;
	if (*x)
	    trail = 1, x++;
	++b;
	i++;
    }

    *b = 0;			/* safe, we skipped the last one above */
    return *x ? -i : i;
}

/** Free NULL-terminated list of strings pointed to by hdl, but do not
    free the handle itself. */
void
free_strlist(/*@null@*/ char **hdl /** string array to free */ )
{
    char **ptr = hdl;

    if (!ptr)
	return;

    while (*ptr) {
	free(*ptr);
	*ptr = NULL;
	ptr++;
    }
}

/** Check if 2nd string is the same as the beginning of first string
 *  case-insensitively. 
 * \return 
 * - true if string begins with prefix
 * - false otherwise 
 */
int
str_isprefix(const char *string, const char *prefix)
{
    return !strncasecmp(string, prefix, strlen(prefix));
}

#ifdef TEST
#include <stdio.h>
int
main(int argc, char **argv)
{
    int n;
    char **x, **y;

    if (argc != 3) {
	puts("usage: strutil <string to break> <separator character list>");
	return 1;
    }

    x = y = str_nsplit(argv[1], argv[2], 20);
    for (n = 0; *x; n++, x++) {
	printf("%2d: %s\n", n, *x);
	free(*x);
    }
    free(y);
    return 0;
}
#endif
