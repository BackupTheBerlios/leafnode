#include "configparam.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <stdio.h>
int
main(int argc, char *argv[])
{
    int i;
    const struct configparam *cp;

    for (i = 1; i <= argc && argv[i]; i++) {
	cp = find_configparam(argv[i]);
	if (cp) {
	    printf("%s: %d\n", cp->name, cp->code);
	} else {
	    printf("%s: (not found)\n", argv[i]);
	}
    }
    exit(0);
}
