#include "leafnode.h"
#include "activutil.h"
#include "ln_log.h"
#include "critmem.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static void xfree(char *d)
{
    if (d)
	free(d);
}

static void
newsgroup_freedata(struct newsgroup *n)
{
    xfree(n->name);
    xfree(n->desc);
}

/* count number of capital letters (isupper) in string */
unsigned long countcaps(const char *s)
{
    unsigned long i = 0;
    while(*s)
    {
	if (isupper((unsigned char)*s))
	    i++;
	s++;
    }
    return i;
}

/* check if d or s has less caps, and copy that part with less caps in
 * its name into d, free the other struct's data
 */
static void picklesscaps(struct newsgroup *d, struct newsgroup *s)
{
    ln_log(LNLOG_SERR, LNLOG_CTOP, "Newsgroup name conflict: %s vs. %s",
	    d->name, s->name);
    if (countcaps(s->name) < countcaps(d->name)) {
	newsgroup_freedata(d);
	newsgroup_copy(d,s);
    } else {
	newsgroup_freedata(s);
    }
    ln_log(LNLOG_SERR, LNLOG_CTOP, "Newsgroup name conflict, chose %s",
	    d->name);
}

void validateactive(void) {
    unsigned long p1, p2;

    for (p1 = p2 = 1 ; p1 < activesize; p1++) {
	if (p1 > p2)
	    newsgroup_copy(&active[p2],&active[p1]);
	if (0 == compactive(&active[p2-1], &active[p1])) {
	    /* duplicate newsgroup */
	    picklesscaps(&active[p2-1],&active[p1]);
	} else {
	    p2++;
	}
    }

    if (p2 < p1) {
	activesize = p2;
	active[p2].name = NULL; /* do NOT free this! it has been copied
				   already */
	active = (struct newsgroup *)critrealloc((char *)active,
		(1 + activesize)
		* sizeof(struct newsgroup),
		"reallocating active");
    }
}
