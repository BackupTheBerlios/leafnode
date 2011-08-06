/* $Id: strlcpy.h,v 1.1 2002/07/05 11:14:27 emma Exp $ */

#ifndef _BSD_STRLCPY_H
#define _BSD_STRLCPY_H

#include "config.h"
#ifndef HAVE_STRLCPY
#include <sys/types.h>
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif /* !HAVE_STRLCPY */

#endif /* _BSD_STRLCPY_H */
