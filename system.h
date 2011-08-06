#ifndef SYSTEM_H
#define SYSTEM_H

#define _GNU_SOURCE 1
#include "config.h"

#include <sys/time.h>
#include <time.h>

#include <inttypes.h>

#if !defined(HAVE_STRCASESTR)
extern char *strcasestr(const char *haystack, const char *needle);
#endif

#endif /* SYSTEM_H */
