#include <unistd.h>
#include "ugid.h"

uid_t
gid_get(void)
{
    return getgid();
}
