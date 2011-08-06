#include "ugid.h"

#include "config.h"
#include <sys/types.h>
#include <sys/param.h>
#include <unistd.h>
#include <grp.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

int
gid_set(gid_t gid)
{
    setgroups(0, &gid);
    return setgid(gid);
}
