#include <pwd.h>
#include "ugid.h"
int
gid_getbyuname(const char *name, /*@out@*/ gid_t * p_gid)
{
    struct passwd *pw;

    pw = getpwnam(name);
    if (!pw)
	return -1;
    *p_gid = pw->pw_gid;
    endpwent();
    return 0;
}
