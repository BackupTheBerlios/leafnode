
#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
/* ensure that the current group IDs are gid
   return  0 for success
   return -1 for error */
int
gid_ensure(gid_t gid)
{
    if (gid_set(gid))
	return -1;
    if (gid_get() != gid)
	return -1;
    return 0;
}
