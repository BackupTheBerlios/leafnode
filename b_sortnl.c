/** \file b_sortnl.c
 *  Ultra-short non-locale-dependent sort replacement.
 *  exits with code 0 for success, code 1 for internal problems (out of memory), *  code 2 for I/O errors.
 * \year 2000 - 2002
 * \author Matthias Andree
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define LINELEN 160


#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "leafnode.h"

static int
comp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int
main(void)
{
    size_t max = 1000;
    char **x = (char **)malloc(max * sizeof(char *));
    size_t count;
    char buf[LINELEN];
    size_t i;

    if (!x)
	exit(1);
    for (count = 0;; count++) {
	if (count == max) {
	    max += max;
	    x = (char **)realloc(x, max * sizeof(char *));

	    if (!x)
		exit(1);
	}
	if (NULL == fgets(buf, LINELEN, stdin))
	    break;

	x[count] = (char *)malloc(strlen(buf) + 1);

	if (!x[count])
	    exit(1);

	strcpy(x[count], buf);
    }

    if (ferror(stdin))
	exit(2);

    if (mergesort(x, count, sizeof(char *), comp))
	qsort(x, count, sizeof(char *), comp);

    for (i = 0; i < count; i++) {
	if (EOF == fputs(x[i], stdout)) {
	    exit(2);
	}
	free(x[i]);
    }
    free(x);
    exit(0);
}
