#include <unistd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
int
uid_set(uid_t uid)
{
    return setuid(uid);
}
