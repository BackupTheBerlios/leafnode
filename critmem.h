#ifndef CRITMEM_H
#define CRITMEM_H

#include <sys/types.h>

void *mycritmalloc(const char *f, long, size_t size, const char *message);
void *mycritcalloc(const char *f, long, size_t size, const char *message);
void *mycritrealloc(const char *f, long, void *a, size_t size,
		    const char *message);
char *mycritstrdup(const char *f, long, const char *, const char *message);
#define critmalloc(a,b) mycritmalloc(__FILE__,__LINE__,a,b)
#define critcalloc(a,b) mycritcalloc(__FILE__,__LINE__,a,b)
#define critrealloc(a,b,c) mycritrealloc(__FILE__,__LINE__,a,b,c)
#define critstrdup(a,b) mycritstrdup(__FILE__,__LINE__,a,b)

#endif
