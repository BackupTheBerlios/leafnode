#include <unistd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "ugid.h"
uid_t
uid_get(void)
{
    return getuid();
}
