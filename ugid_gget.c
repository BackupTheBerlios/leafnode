#include <unistd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
uid_t
gid_get(void)
{
    return getgid();
}
