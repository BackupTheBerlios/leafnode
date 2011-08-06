/*
 * (C) 2001 by Matthias Andree
 */

/*
 * This file (leafnode-version.c) is public domain. It comes without and
 * express or implied warranties. Do with this file whatever you wish, but
 * don't remove the disclaimer.
 */

#include "leafnode.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "config.h"

int
main(void) /* if you support arguments some day, please make -v
	      compatible with leafnode-1. */
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
    fputs("default spooldir: ", stdout);
    puts(def_spooldir);
    fputs("default MTA: ", stdout);
    puts(DEFAULTMTA);
    fputs("pcre version: ", stdout);
    puts(pcre_version());
    exit(0);
}
