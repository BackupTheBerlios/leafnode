/** \file mkdir_parent.c
 * \author Matthias Andree
 * \year 2001
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "ln_dir.h"

/** mkdir -p like function, but we only treat parents,
 * so passing in a file name is safe.
 */
int mkdir_parent(const char *s, mode_t mode) {
    char *t = strdup(s);
    char *u = t;
    if (!t)
	return -1;

    while (*u && (u = strchr(u + 1, '/'))) {
	*u = '\0';
	if (*t && mkdir(t, mode) && errno != EEXIST) {
	    free(t);
	    return -1;
	}
	*u = '/';
    }
    free(t);
    return 0;
}
