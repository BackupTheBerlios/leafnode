#include "leafnode.h"
#include "attributes.h"
#include "critmem.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <string.h>

/*@null@*/ /*@only@*/ char *
mygetfoldedline(const char *fi, unsigned long ln, FILE * f)
{
    /* what characters are considered whitespace that marks the beginning of
       continuation lines.
       WARNING: NEVER EVER list \n here! */
    static const char white[] = " \t";
    char *l1, *l2;
    int c, len, oldlen;

    l1 = getaline(f);
    if (!l1)
	return NULL;
    l2 = (char *)mycritmalloc(fi, ln, (len = strlen(l1)) + 1, "getfoldedline");
    strcpy(l2, l1);

    /* only try to read continuation if the line is not empty 
     * and not a lone dot */
    if (*l2 && strcmp(l2, ".")) {
	for (;;) {
	    c = fgetc(f);
	    if (c != EOF) {
		ungetc(c, f);
		if (strchr(white, c)) {
		    /* join */
		    l1 = getaline(f);
		    if (l1) {
			oldlen = len;
			len += strlen(l1);
			l2 = (char *)mycritrealloc(fi, ln, l2, len + 1,
						   "getfoldedline");
			strcpy(l2 + oldlen, l1);
		    }
		} else {
		    break;
		}
	    } else {
		break;
	    }
	}
    }
    return l2;
}

#ifdef TEST
int
main()
{
    char *f;
    while ((f = getfoldedline(stdin))) {
	puts(f);
	free(f);
    }
    return 0;
}
#endif
