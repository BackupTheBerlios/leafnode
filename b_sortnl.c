/* ultra-short non-locale-dependent sort replacement 
 * (C) 2000 Matthias Andree */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINELEN 160

static int
comp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

int
main(void)
{
    int max = 1000;
    char **x = malloc(max * sizeof(char *));
    char **y = x;
    int count = 0;
    char buf[LINELEN];
    int i;
    if (!x)
	exit(1);

    for (;;) {
	if (count == max) {
	    max += max;
	    x = realloc(x, max * sizeof(char *));
	    if (!x)
		exit(1);
	}
	if (!fgets(buf, LINELEN, stdin))
	    break;
	*y = malloc(strlen(buf) + 1);
	if (!*y)
	    exit(1);
	strcpy(*y, buf);
	count++;
	y++;
    }
    if (ferror(stdin))
	exit(2);
    qsort(x, count, sizeof(char *), comp);
    for (i = 0, y = x; i < count; i++) {
	fputs(*y, stdout);
	free(*y);
	y++;
    }
    free(x);
    exit(0);
}
