#ifndef GETLINE_H
#define GETLINE_H

#include <sys/types.h>
#include "config.h"

/* from getline.c */
#ifndef HAVE_GETLINE
ssize_t getline(char **, size_t *, FILE *); /* fgets replacement */
#endif

#endif
