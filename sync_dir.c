#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include "leafnode.h"
#include "mastring.h"

int
sync_dir(const char *in)
{
    int fd, r, e;

    fd = open(in, O_RDONLY);
    if (fd < 0)
	return fd;

    r = fsync(fd);
    e = errno;
    (void)close(fd);
    errno = e;
    return r;
}

int
sync_parent(const char *in)
{
    char s[PATH_MAX];
    char *r;

    /* copy just the dirname to s */
    if ((r = strrchr(in, '/'))) {
	mastrncpy(s, in, r - in + 1);
    } else {
	mastrcpy(s, ".");
    }

    return sync_dir(s);
}