#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "ln_log.h"

/*
 * replacement for realloc, logs allocation failures
 * and exits with the error message
 */
char * critrealloc(char *a, size_t size, const char* message) {
    a = (char *)realloc(a, size);
    if (!a) {
	ln_log(LNLOG_ERR, "realloc(%d) failed: %s", (int)size, message);
	exit(1);
    }
    return a;
}
