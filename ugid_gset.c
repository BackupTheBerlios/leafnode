#include <unistd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
int
gid_set(uid_t gid)
{
    return setgid(gid);
}
