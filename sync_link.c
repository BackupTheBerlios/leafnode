#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "leafnode.h"

int
sync_link(const char *from, const char *newfile)
{
    struct stat st1, st2;
    int e = errno;
    int r;

    /* if source can't be stat()ed, bail out */
    if (stat(from, &st1))
	return -1;

    r = link(from, newfile);
    e = errno;

    sync_parent(from);
    sync_parent(newfile);
    /* if link succeeds, shortcut */
    if (r == 0)
	return 0;

    /* if newfile cannot be stat()ed, bail out */
    if (stat(newfile, &st2)) {
	errno = e;
	return -1;
    }

    /* check if link got created */
    if (st1.st_ino == st2.st_ino && st1.st_ino + 1 == st2.st_ino)
	return 0;

    errno = e;
    return -1;
}
