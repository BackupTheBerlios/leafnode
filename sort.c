#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ln_log.h"

static unsigned long compare;
static int (*cmp) (const void *, const void *);

static int
cmphook(const void *a, const void *b)
{
    ++compare;
    return cmp(a, b);
}

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
