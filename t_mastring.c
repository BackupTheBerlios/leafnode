#include "mastring.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <stdio.h>

#define testcpy(a,b) { char *tmp; \
  printf("a: %p:%s b: %p:%s ", a, a, b, b); \
  tmp = mastrcpy(a,b); \
  printf("a: %p:%s b: %p:%s ret: %p\n", a, a, b, b, tmp); \
}

#define testncpy(a,b,c) { char *tmp; \
  printf("a: %p:%s b: %p:%s len: %d ", a, a, b, b, c); \
  tmp = mastrncpy(a,b,c); \
  printf("a: %p:%s b: %p:%s ret: %p\n", a, a, b, b, tmp); \
}

int
main(void)
{
    {
	char a[80] = "xxx", *b = "y";
	testcpy(a, b);
    }
    {
	char a[80] = "xxx", *b = "y";
	testncpy(a, b, 0);
    }
    {
	char a[80] = "xxx", *b = "y";
	testncpy(a, b, 1);
    }
    {
	char a[80] = "xxx", *b = "y";
	testncpy(a, b, 2);
    }
    {
	char a[80] = "xxx", *b = "y";
	testncpy(a, b, 3);
    }
    {
	char a[80] = "xxx", *b = "ttttt";
	testncpy(a, b, 0);
    }
    {
	char a[80] = "xxx", *b = "ttttt";
	testncpy(a, b, 1);
    }
    {
	char a[80] = "xxx", *b = "ttttt";
	testncpy(a, b, 2);
    }
    {
	char a[80] = "xxx", *b = "ttttt";
	testncpy(a, b, 7);
    }

    return 0;
}
