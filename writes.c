#include <string.h>
#include <unistd.h>
#include "leafnode.h"

ssize_t
writes(int fd, const char *string)
{
    return write(fd, string, strlen(string));
}
