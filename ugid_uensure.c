
#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
/* ensure that the current user IDs are uid
   return  0 for success
   return -1 for error */
int
uid_ensure(uid_t uid)
{
    if (uid_set(uid))
	return -1;
    if (uid_get() != uid)
	return -1;
    return 0;
}
