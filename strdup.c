#include "config.h"

#ifndef HAVE_STRDUP

#include <string.h>
#include <stdlib.h>

char *
strdup(const char *s)
{
    char *s1;
    s1 = malloc(strlen(s) + 1);
    if (!s1)
	return 0;
    strcpy(s1, s);
    return s1;
}

#endif

/* ANSI C forbids en empty source file... */
static void
dummy_func(void)
{
    dummy_func();
}
