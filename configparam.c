/** \file configparam.c
 * (C) 2001 - 2002 by Matthias Andree
 */
#include "config_defs.h"
#include "configparam.h"
#include <stdlib.h>
#include <string.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static int
comp(const void *a, const void *b)
{
    return strcmp((const char *)a, ((const struct configparam *)b)->name);
}

/*@null@*/ /*@dependent@*/ const struct configparam *
find_configparam(register const char *name)
{
    return (const struct configparam *)
	bsearch(name, configparam, count_configparam,
		sizeof(struct configparam), comp);
}
