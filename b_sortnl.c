/** \file b_sortnl.c
 *  Ultra-short non-locale-dependent sort replacement.
 *  exits with code 0 for success, code 1 for internal problems (out of memory), *  code 2 for I/O errors.
 * \date 2000 - 2002
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
#include "critmem.h"
#include "ln_log.h"

static int
b_comp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void ln_log(int a, int b, const char *c, ...) {
/* dummy */
    (void)a;
    (void)b;
    (void)c;
}

int
main(void)
{
    size_t max = 1000;
    char **x = (char **)critmalloc(max * sizeof(char *), "b_sortnl");
    size_t count;
    char buf[LINELEN];
    size_t i;

    for (count = 0;; count++) {
	if (count == max) {
	    max += max;
	    x = (char **)critrealloc(x, max * sizeof(char *), "b_sortnl");

	    if (!x)
		exit(1);
	}
	if (NULL == fgets(buf, LINELEN, stdin))
	    break;

	x[count] = (char *)critmalloc(strlen(buf) + 1, "b_sortnl");

	if (!x[count])
	    exit(1);

	strcpy(x[count], buf);
    }

    if (ferror(stdin))
	exit(2);

    if (mergesort(x, count, sizeof(char *), b_comp))
	qsort(x, count, sizeof(char *), b_comp);

    for (i = 0; i < count; i++) {
	if (EOF == fputs(x[i], stdout)) {
	    exit(2);
	}
	free(x[i]);
    }
    free(x);
    exit(0);
}
