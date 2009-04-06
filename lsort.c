/*
 * lsort - re-sort groupinfo files from versions prior to 1.9.3
 *
 * Written by Joerg Dietrich <joerg@dietrich.net>
 * (C) Copyright 1999
 *
 * Bug fixes by Ralf Wildenhues, 2002
 *
 * Bug fixes by Matthias Andree, 2002, 2009
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
#include <errno.h>

static int
lcomp(const void *a, const void *b)
{
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

int
main(void)
{
    char *l;
    char **act;
    size_t acount = 0;
    size_t i;
    FILE *f;
    int err = 0;
    mastr *path = mastr_new(LN_PATH_MAX);

    (void)mastr_vcat(path, def_spooldir, "/leaf.node/groupinfo.old", NULL);

    f = fopen(mastr_str(path), "r");
    if (f == NULL) {
	fprintf(stderr, "Error: Can't open \"%s\": %s!\n", mastr_str(path), strerror(errno));
	exit(EXIT_FAILURE);
    }

    act = (char **)critmalloc(sizeof(char *),
			      "Allocating space for active file");

    while ((l = getaline(f))) {
	act = (char **)realloc(act, (acount + 1) * sizeof(char *));
	if (act == NULL) {
	    fprintf(stderr, "out of memory: realloc returned NULL\n");
	    exit(EXIT_FAILURE);
	}
	act[acount] = critstrdup(l, "lsort");
	acount++;
    }
    (void)fclose(f);

    qsort(act, acount, sizeof(char *), &lcomp);

    clearerr(stdout);
    for (i = 0; i < acount; i++) {
	int ok;
	ok = printf("%s\n", act[i]);
	if (ok < 0) { err = 1; break; }
    }

    free(act);
    mastr_delete(path);
    fflush(stdout);
    if (ferror(stdout)) err = 1;
    return err ? EXIT_FAILURE : EXIT_SUCCESS;
}
