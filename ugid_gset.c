#include <unistd.h>
#include "ugid.h"

int
gid_set(uid_t gid)
{
    return setgid(gid);
}
