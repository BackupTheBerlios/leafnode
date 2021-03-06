#include "leafnode.h"
#include "ln_log.h"
#include "mastring.h"

#include <stdarg.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <stdio.h>

/*
 * 05/26/97 - T. Sweeney - Send a string out, keeping a copy in reserve.
 */
void
putaline(FILE * f, const char *fmt, ...)
{
    char lineout[1025];
    va_list args;

    va_start(args, fmt);
    vsnprintf(lineout, sizeof(lineout), fmt, args);
    if (debugmode & DEBUG_IO)
	ln_log(LNLOG_SDEBUG, LNLOG_CALL, ">%s", lineout);
    (void)mastrncpy(last_command, lineout, sizeof(lineout));
    fprintf(f, "%s\r\n", lineout);
    fflush(f);
    va_end(args);
}
