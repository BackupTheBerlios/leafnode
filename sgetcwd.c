#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "critmem.h"
#include "ln_log.h"
#include "sgetcwd.h"

const char *sgetcwd(void) {
    static char *buf;
    static size_t size = 128;

    for(;;) {
	if (!buf)
	    buf = (char *)critmalloc(size, "sgetcwd");

	if (getcwd(buf, size))
	    return buf;

	switch(errno) {
	    case ERANGE:
		size += size;
		free(buf);
		buf = NULL;
		break;
	    case EACCES:
		return "(unknown directory - EACCES)";
	    case EINVAL:
	    case ENOMEM:
	    default:
		ln_log(LNLOG_SERR, LNLOG_CTOP,
			"getcwd() failed: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
    }
}
