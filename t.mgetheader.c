#include "leafnode.h"
#include <stdlib.h>

int main(void) {
    const char *haystack = "X: y\nMessage-Id: teSt\n";
    const char *needle = "Message-ID:";
    char *res;
    int rc = EXIT_SUCCESS;

    res = mgetheader(needle, haystack);
    if (!res || 0 != strcmp(res, "teSt"))
	rc = EXIT_FAILURE;
    if (res)
	free(res);
    exit(rc);
}
