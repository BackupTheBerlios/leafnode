/** \file parserange.c
 *  Parse a range of format [n1][-][n2] as used in NNTP XOVER
 *  \author Matthias Andree
 *  \year 2001
 */

#include <string.h>
#include <ctype.h>

#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "get.h"

/** extract lower and upper bounds from string if given. Do not modify
 *  \p from or \p to if corresponding limit has not been given. The caller 
 * should initialize \p from and \p to to reasonable values.
 * \return 
 *  - if return & RANGE_ERR, error occured, result invalid
 *  - if return & RANGE_HAVEFROM, the lower bound has been specified and
 *  placed * into \p from
 *  - if return & RANGE_HAVETO, the upper bound has been specified and
 *  placed into \p to 
 */
int
parserange(const char *in /** input string */ , unsigned long *from,
	   unsigned long *to)
{
    int i;
    int res = 0;
    char *a[4];

    if (!in)
	return RANGE_ERR;
    i = str_nsplit(a, in, "-", sizeof(a) / sizeof(a[0]));
    if (i < 0)
	return RANGE_ERR;
    if (i > 2) {
	free_strlist(a);
	return RANGE_ERR;
    }

    if (a[0]) {
	res |= RANGE_HAVEFROM;
	if (a[0][0]) {
	    if (!(get_ulong(a[0], from)))
		res |= RANGE_ERR;
	}
    }

    if (a[1]) {
	res |= RANGE_HAVETO;
	if (a[1][0]) {
	    if (!(get_ulong(a[1], to)))
		res |= RANGE_ERR;
	}
    }
    free_strlist(a);
    return res;
}

#ifdef TEST
int debug = 0;

int
main(int argc, char **argv)
{
    unsigned long a, b;
    int c;

    c = parserange(argv[1], &a, &b);

    printf("result: %d, a: %lu, b: %lu\n", c, a, b);
    return 0;
}
#endif
