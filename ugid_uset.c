#include <unistd.h>
#include "ugid.h"

int
uid_set(uid_t uid)
{
    return setuid(uid);
}
