#include "leafnode.h"

time_t
lookup_expire(char *group)
{
    struct expire_entry *a;

    for (a = expire_base; a; a = a->next) {
	if (ngmatch(a->group, group) == 0)
	    return a->xtime;
    }
    return default_expire;
}
