/*
 * (C) 2001 by Matthias Andree 
 */

/*
 * This file (leafnode-version.c) is public domain. It comes without and
 * express or implied warranties. Do with this file whatever you wish, but
 * don't remove the disclaimer. 
 */

#include <stdio.h>
#include "leafnode.h"
#include "config.h"

int
main(void)
{
    puts(compileinfo);
    fputs("current machine: ", stdout);
    fflush(stdout);
    if (system("/bin/uname -a"))
	puts(" (error)");
    fputs("bindir: ", stdout);
    puts(bindir);
    fputs("libdir: ", stdout);
    puts(libdir);
    fputs("spooldir: ", stdout);
    puts(spooldir);
    fputs("lockfile: ", stdout);
    puts(lockfile);
#ifdef HAVE_IPV6
    puts("IPv6: yes");
#else
    puts("IPv6: no");
#endif
    exit(0);
}
