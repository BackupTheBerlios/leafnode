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

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "config.h"

int
main(void)
{
    static char env_path[] = "PATH=/bin:/usr/bin";
    /* ------------------------------------------------ */
    /* *** IMPORTANT ***
     * 
     * external tools depend on the first line of this output, which is
     * in the fixed format
     * version: leafnode-2.3.4...
     */
    fputs("version: leafnode-", stdout);
    puts(version);
    /* changable parts below :-) */
    fputs("current machine: ", stdout);
    fflush(stdout);
    putenv(env_path);
    if (system("uname -a"))
	puts(" (error)");
    fputs("bindir: ", stdout);
    puts(bindir);
    fputs("sysconfdir: ", stdout);
    puts(sysconfdir);
    fputs("spooldir: ", stdout);
    puts(spooldir);
    fputs("lockfile: ", stdout);
    puts(lockfile);
#ifdef HAVE_IPV6
    puts("IPv6: yes");
#else
    puts("IPv6: no");
#endif
    fputs("default MTA: ", stdout);
    puts(DEFAULTMTA);
    exit(0);
}
