#ifndef GETLINE_H
#define GETLINE_H
#include <sys/types.h>
#include "config.h"

ssize_t _getline( /*@out@*/ char *to, size_t size, FILE * stream);

#ifndef HAVE_GETLINE
ssize_t getline( /*@out@*/ char **, size_t *, FILE *);	/* fgets replacement */
#endif
#endif
