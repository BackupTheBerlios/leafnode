#include <unistd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
int
gid_set(gid_t gid)
{
    setgroups(0, &gid);
    return setgid(gid);
}
