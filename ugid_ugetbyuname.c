#include <pwd.h>
#include "ugid.h"

int uid_getbyuname(const char *name, uid_t *p_uid) {
    struct passwd *pw;
    pw=getpwnam(name);
    if(!pw) return -1;
    *p_uid=pw->pw_uid;
    return 0;
}