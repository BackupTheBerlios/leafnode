#include "critmem.h"
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

/*
 * replacement for malloc, syslogs allocation failures
 * and exits with the error message
 */
char * critmalloc(size_t size, const char* message) {
    char * a;

    a = malloc(size);
    if (!a) {
	syslog(LOG_ERR, "malloc(%d) failed: %s", (int)size, message);
	fprintf(stderr, "malloc(%d) failed: %s\n", (int)size, message);
	exit(1);
    }
    return a;
}

/*
 * replacement for realloc, syslogs allocation failures
 * and exits with the error message
 */
char * critrealloc(char *a, size_t size, const char* message) {
    a = realloc(a, size);
    if (!a) {
	syslog(LOG_ERR, "realloc(%d) failed: %s", (int)size, message);
	fprintf(stderr, "realloc(%d) failed: %s\n", (int)size, message);
	exit(1);
    }
    return a;
}
