#ifndef CRITMEM_H
#define CRITMEM_H

#include <sys/types.h>

/*@only@*//*@out@ */ void *mycritmalloc(const char *f, long, size_t size,
					const char *message);
/*@only@*/ void *mycritcalloc(const char *f, long, size_t size,
			      const char *message);
/*@only@*//*@out@ *//*@notnull@ */ void *mycritrealloc(const char *f, long,
/*@null@ *//*@only@ *//*@out@ *//*@returned@ */
						       void *a, size_t size,
						       const char *message);
/*@only@*/ char *mycritstrdup(const char *f, long, const char *,
			      const char *message);
#define critmalloc(a,b) mycritmalloc(__FILE__,(long)__LINE__,a,b)
#define critcalloc(a,b) mycritcalloc(__FILE__,(long)__LINE__,a,b)
#define critrealloc(a,b,c) mycritrealloc(__FILE__,(long)__LINE__,a,b,c)
#define critstrdup(a,b) mycritstrdup(__FILE__,(long)__LINE__,a,b)

#endif
