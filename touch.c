#include "leafnode.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/** Create a file and make sure it has a current mtime. Note this file
    is not fsynced. */
int
touch(const char *name)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int r, e;

    if (fd >= 0) {
	/* actually update mtime */
	(void)write(fd, "*", 1);
	r = ftruncate(fd, 0);
	e = errno;
	(void)close(fd);
	errno = e;
	return r;
    } else {
	return fd;
    }
}
