/** \file b_sortnl.c
 *  ultra-short non-locale-dependent sort replacement
 * \year 2000
 * \author Matthias Andree
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#define LINELEN 160

#include "leafnode.h"

static int
comp(const void *a, const void *b)
{
    return strcmp(*(const char *const *const)a, *(const char *const *const)b);
}

int
main(void)
{
    int max = 1000;
    char **x = (char **)malloc(max * sizeof(char *));
    char **y = x;
    int count = 0;
    char buf[LINELEN];
    int i;

    if (!x)
	exit(1);
    for (;;) {
	if (count == max) {
	    max += max;
	    x = (char **)realloc(x, max * sizeof(char *));

	    if (!x)
		exit(1);
	}
	if (!fgets(buf, LINELEN, stdin))
	    break;
	*y = (char *)malloc(strlen(buf) + 1);
	if (!*y)
	    exit(1);
	strcpy(*y, buf);	/* Flawfinder: ignore -- this is allocated */
	count++;
	y++;
    }
    if (ferror(stdin))
	exit(2);

    if (mergesort(x, count, sizeof(char *), comp))
	qsort(x, count, sizeof(char *), comp);

    for (i = 0, y = x; i < count; i++) {
	fputs(*y, stdout);
	free(*y);
	y++;
    }
    free(x);
    exit(0);
}
