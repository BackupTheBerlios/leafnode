#include "config_defs.h"
#include "configparam.h"
#include <stdlib.h>

static int
comp(const void *a, const void *b)
{
    return strcmp((char *)a, ((struct configparam *)b)->name);
}

const struct configparam *
find_configparam(register const char *name)
{
    return (struct configparam *)
	bsearch(name, configparam, count_configparam,
		sizeof(struct configparam), comp);
}
