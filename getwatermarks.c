#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>
#include "leafnode.h"

/** Get the current first and last article number, and possibly the
 * count, and store them into f, l and possibly count. Returns 0 for
 * success, -1 in case of trouble.
 */
int getwatermarks(unsigned long *f, unsigned long *l,
		  unsigned long /*@null@*/ *c) {
    char *q;
    unsigned long first = ULONG_MAX;
    unsigned long last = 0, count = 0;
    DIR *ng;
    struct dirent *nga;

    ng = opendir(".");
    if (ng == NULL) {
	return -1;
    }

    while ((nga = readdir(ng)) != NULL) {
	/* check if name is all-numeric */
	int allnum = 1;
	char *x;
	for (x = nga->d_name; *x != '\0'; x++) {
	    if (!isdigit((unsigned char)*x)) {
		allnum = 0;
		/*@innerbreak@*/ break;
	    }
	}

	if (allnum) {
	    unsigned long i;

	    count ++;
	    i = strtoul(nga->d_name, &q, 10);
	    if (q != NULL && *q == '\0') {
		if (i < first) {
		    first = i;
		}
		if (i > last) {
		    last = i;
		}
	    }
	}
    }
    if (first > last) {
	first = last = 1ul;
    }
    if (closedir(ng) != 0) {
	return -1;
    }
    *f = first;
    *l = last;
    if (c) {
	*c = count;
    }
    return 0;
}
