/** \file bsearch_range.c
 * binary search for an interval of values in a sorted array,
 * \author Ralf Wildenhues
 * \date 2002
 */

#include <stddef.h>
#include "bsearch_range.h"

#define MAX(a,b) ((a)>(b) ? (a) : (b))

/**
 * Search for an interval [ \p key1, \p key2 ] of values in the sorted array
 * \p base, consisting of \p nmemb items of size \p size each (like bsearch).
 * Return inclusive limits (the biggest matching range) in pointers
 * \p p1 and \p p2 (cast to const void **).
 * On error (empty interval found or \p key1 > \p key2), \p p1 or \p p2 will
 * contain NULL.
 */
void
bsearch_range(const void *key1, const void *key2,
	      /*@out@*/ void *p1 /** address of pointer to point to lower bound */,
	      /*@out@*/ void *p2 /** address of pointer to point to upper bound */,
	      const void *base, size_t nmemb, size_t size,
	      int (*cmp)(const void *, const void *))
{
    const char *b = (const char *)base;
    size_t l1, u1, l2, u2;

    l1 = 0;
    u1 = nmemb;
    while (l1 < u1) {
	size_t idx = (l1 + u1) / 2;
	int comp = (*cmp)(key1, b + idx * size);
	if (comp <= 0)
	    u1 = idx;
	else
	    l1 = idx + 1;
    }
    if (l1 < nmemb)
	*(const void **)p1 = b + l1 * size;
    else
	*(const void **)p1 = NULL;

    if (((*cmp)(key1, key2)) > 0) {
	*(const void **)p2 = NULL;
	return;
    }

    l2 = u1;
    u2 = nmemb;
    while (l2 < u2 && l2 < nmemb) {
	size_t idx = (l2 + u2) / 2;
	int comp = (*cmp)(key2, b + idx * size);
	if (comp < 0)
	    u2 = idx;
	else
	    l2 = idx + 1;
    }

    if (l2 == nmemb || ((*cmp)(key2, b + l2 * size)) < 0) {
	if (l2 > u1)
	    *(const void **)p2 = b + (l2-1) * size;
	else
	    *(const void **)p2 = NULL;
    } else
	*(const void **)p2 = b + l2 * size;
}

#ifdef TEST_BSEARCH_RANGE
#include <stdio.h>

static
int cmp_int(const void *p, const void *q)
{
    int i = *(const int *)p, j = *(const int *)q;
    if      (i<j) return -1;
    else if (i>j) return  1;
    else          return  0;
}

int
main(void)
{
    int a[] = {1,4,4,7,7,8,10,10,12,12,12};
    int key1 = 3, key2 = 10;
    int *p1, *p2 = NULL, i1, i2;
    bsearch_range(&key1, &key2, &p1, &p2,
	          a, sizeof a / sizeof a[0], sizeof a[0], cmp_int);
    if (p1) i1 = p1-a;
    else    i1 = -1;
    if (p2) i2 = p2-a;
    else    i2 = -1;
    printf("i1=%d\ni2=%d\na[i1]=%d\na[i2]=%d\n",
	    i1, i2, i1 >= 0 ? a[i1] : -1, i2 >= 0 ? a[i2] : -1);

    return 0;
}
#endif /* TEST_BSEARCH_RANGE */
