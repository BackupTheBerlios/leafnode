#include "format.h"
#include <stdio.h>
#include <limits.h>
int
main()
{
    char b[20];

#define mytest(x) {str_ulong(b, (x)); puts(b); }
    mytest(0);
    mytest(1);
    mytest(9);
    mytest(10);
    mytest(50);
    mytest(ULONG_MAX);
    _exit(0);
}
