#include <unistd.h>
#include "ugid.h"

uid_t
uid_get(void)
{
    return getuid();
}
