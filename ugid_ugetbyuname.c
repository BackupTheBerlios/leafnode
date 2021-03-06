#include "ugid.h"

#include <pwd.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

int
uid_getbyuname(const char *name, /*@out@*/ uid_t * p_uid)
{
    struct passwd *pw;

    pw = getpwnam(name);
    if (!pw)
	return -1;
    *p_uid = pw->pw_uid;
    endpwent();
    return 0;
}
