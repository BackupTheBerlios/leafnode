#ifndef BSEARCH_RANGE_H
#define BSEARCH_RANGE_H

#include <stddef.h>	/* size_t */

void
bsearch_range(const void *key1, const void *key2,
	      /*@out@*/ void *p1, /*@out@*/ void *p2,
	      const void *base, size_t nmemb, size_t size,
	      int (*cmp) (const void *, const void *));

#endif /* BSEARCH_RANGE_H */
