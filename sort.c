/** \file
 * Leafnode sort function wrapper, with comparison counting in debug 
 * mode.
 */
/* (C) Copyright 2001 Matthias Andree
 * with fixes 2002, 2003, 2004 and documentation 2009. 
 * See COPYING for the license. */
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

/** Counter for number of comparisons, incremented by cmphook(). */
static unsigned long compare;
/** Variable for comparison function, passed to sort algorithms qsort() 
 * and mergesort(), and set by my_sort().
 */
static int (*cmp) (const void *, const void *);

/** Compare hook that increases the static counter compare.
 * Used as wrapper around the real comparison function for evaluation. */
static int
cmphook(const void *a, const void *b)
{
    ++compare;
    return cmp(a, b);
}

/**
 * Leafnode sort wrapper function, with mergesort-to-qsort fallback and 
 * evaluation facility to count comparisons. This function will default 
 * to trying mergesort() first, which can fail either because the data 
 * elements are too small (this won't happen in leafnode) or because 
 * there is not enough heap memory. In this case, this wrapper will fall 
 * back to qsort(), which does not need additional memory except stack.
 *
 * The mergesort() used here is derived from BSD and has O(N) best-case 
 * and O(N log N) worst-case behaviour; and it's good for pre-sorted 
 * data, which is a common case in leafnode. See mergesort(3) for 
 * details.
 */
void
my_sort(void *base, size_t nmemb, size_t size,
      int (*compar) (const void *, const void *),
      const char *file, unsigned long line)
{
    if (debugmode & DEBUG_SORT) {
	compare = 0;
	cmp = compar;

	if (nmemb < 2) return;
	if (mergesort(base, nmemb, size, cmphook)) {
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
		   "quicksort(base=%p, nmemb=%lu, "
		   "size=%lu)", base,
		   (unsigned long)nmemb, (unsigned long)size);
	    qsort(base, nmemb, size, cmphook);
	} else {
	    ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
		   "mergesort(base=%p, nmemb=%lu, "
		   "size=%lu)", base,
		   (unsigned long)nmemb, (unsigned long)size);
	}

	ln_log(LNLOG_SDEBUG, LNLOG_CTOP,
	       "sort took %lu comparisons, called from %s:%lu", compare,
	       file, line);
    } else {
	/* not in debug mode */
	if (nmemb < 2) return;
	if (mergesort(base, nmemb, size, compar)) {
	    qsort(base, nmemb, size, compar);
	}
    }
}
