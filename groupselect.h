#ifndef GROUPSELECT_H
#define GROUPSELECT_H

/* for pcre type */
#include <pcre.h>

pcre *gs_compile(const char *regex, const char *file, unsigned long line);
int gs_match(const pcre *p, const char *s);

#endif
