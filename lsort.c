/*
 * lsort - re-sort groupinfo files from versions prior to 1.9.3
 *
 * Written by Joerg Dietrich <joerg@dietrich.net>
 * (C) Copyright 1999
 *
 * See file COPYING for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "mastring.h"

#include <stdlib.h>
#include <string.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <stdio.h>

static int
comp(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

int
main(void)
{
    char *l;
    char path[PATH_MAX];
    char **act;
    size_t acount = 0;
    size_t i;
    FILE *f;

    l = mastrcpy(path, spooldir);
    (void)mastrcpy(l, "/leaf.node/groupinfo.old");

    f = fopen(path, "r");
    if (f == NULL) {
	fprintf(stderr, "Error: Can't open groupinfo.old!\n");
	exit(EXIT_FAILURE);
    }

    act = (char **)critmalloc(sizeof(char *),
			      "Allocating space for active file");

    while ((l = getaline(f))) {
	act = (char **)realloc(act, (acount + 1) * sizeof(char *));
	if (act == NULL) {
	    fprintf(stderr, "realloc returned NULL\n");
	    exit(EXIT_FAILURE);
	}
	act[acount] = critstrdup(l, "lsort");
	acount++;
    }
    (void)fclose(f);

    qsort(act, acount, sizeof(char *), &comp);

    for (i = 0; i < acount; i++)
	printf("%s\n", act[i]);

    free(act);
    return 0;
}
