#include <sys/types.h>
#include <dirent.h>
#include "ln_log.h"
#include "leafnode.h"
#include <errno.h>
#include <string.h>

DIR *log_open_dir(const char *name) {
    DIR *d;
    d=opendir(name);
    if(!d) {
	ln_log(LNLOG_ERR, "Unable to opendir %s: %s\n", 
	      spooldir, strerror(errno));
	return 0;
    }
    return d;
}

DIR *log_open_spool_dir(const char *name) {
    char x[PATH_MAX];
    if(snprintf(x, sizeof(x), "%s/%s", spooldir, name) >= 0) {
	return log_open_dir(x);
    }
    return 0;
}

