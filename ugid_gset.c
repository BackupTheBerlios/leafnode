#include "config.h"
#include <sys/types.h>
#include <unistd.h>
#if HAVE_GRP_H
# include <grp.h>
#endif

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
