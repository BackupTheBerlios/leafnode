#include <unistd.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/* returns number < 0 for trouble, count otherwise */
ssize_t
nfswrite(int fd, const void *b, size_t count)
{
    ssize_t s;

    s = write(fd, b, count);
    if (s < (ssize_t) count)
	return s > 0 ? -1 : s;

    s = fsync(fd);
    if (s < 0)
	return s;

    return count;
}

/* returns 0 for success, negative number otherwise */
ssize_t
nfswriteclose(int fd, const void *b, size_t count)
{
    ssize_t s;

    s = nfswrite(fd, b, count);
    if (s < (ssize_t) count)
	return s > 0 ? -1 : s;

    s = close(fd);
    if (s < 0)
	return s;

    return 0;
}
