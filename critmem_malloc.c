#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include "ln_log.h"

/*
 * replacement for malloc, logs allocation failures
 * and exits with the error message
 */
char * critmalloc(size_t size, const char* message) {
    char * a;

    a = (char *)malloc(size);
    if (!a) {
	ln_log(LNLOG_ERR, "malloc(%d) failed: %s", (int)size, message);
	exit(1);
    }
    return a;
}
