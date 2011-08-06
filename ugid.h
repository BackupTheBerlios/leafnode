#ifndef UGID_H
#define UGID_H

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include <sys/types.h>
extern int uid_ensure(uid_t);
extern int uid_set(uid_t);
extern uid_t uid_get(void);
extern int uid_getbyuname(const char *, /*@out@*/ uid_t *);
extern int gid_ensure(gid_t);
extern int gid_set(gid_t);
extern gid_t gid_get(void);
extern int gid_getbyuname(const char *, /*@out@*/ gid_t *);
extern int gid_getbygname(const char *, gid_t *);
#endif
