#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static int rc = EXIT_SUCCESS;

static void complain(const char *t, int r, const char *s, int er, const char *es) {
	fprintf(stderr, "test %s returned %d, string: \"%s\", wanted %d/\"%s\"\n", t, r, s, er, es);
	rc = EXIT_FAILURE;
}

static void test(const char *t, int er, const char *es, int siz, 
	const char *f, ...) {
    va_list ap;
    int r;

    char s[40];
    const char init[40] = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
    va_start(ap, f);
    strcpy(s, init);
    r = vsnprintf(s, siz, f, ap);
    va_end(ap);
    if (r != er || strcmp(s, es))
	complain(t, r, s, er, es);
}

int main(void)
{
    test ("1a", 1, "", 1, "1");
    test ("1b", 1, "1", 2, "%d", 1);
    test ("1c", 2, "1", 2, "%d", 11);
    test ("2", 1, "1", 2, "1");
    test ("3a", 1, "1", 2, "%s", "1");
    test ("3b", 1, "1", 2, "%c", '1');
    test ("3c", 1, "1", 2, "%d", 1);
    test ("3d", 1, "1", 2, "%ld", 1l);
    test ("3e", 1, "1", 2, "%u", 1);
    test ("3f", 1, "1", 2, "%lu", 1l);
    test ("4", 0, "", 1, "");
    test ("5", 5, "64738", 10, "%u", 64738);
    test ("6a", 3, "003", 4, "%03lu", 3l);
    test ("6b", 3, "  3", 4, "%3lu", 3l);
    test ("6c", 3, "", 1, "%3lu", 3l);
    test ("7a", 3, "22", 3, "%3d", 222);
    test ("7b", 3, "222", 4, "%3d", 222);
    test ("7c", 4, "2222", 6, "%3d", 2222);
    return rc;
}
