/* extracted from fetchnews.c by Ralf Wildenhues */

#include <sys/types.h>
#include <sys/stat.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "mastring.h"
#include "ln_log.h"


/** Strip off directories from file and move that file to dir below
 * spooldir, logging difficulties.
 * \return the return value of the embedded rename(2) syscall
 */
int
log_moveto(/*@notnull@*/ const char *file /** source file */ ,
       const char *dir /** destination directory */ )
{
    int r, e;

    mastr *s = mastr_new(LN_PATH_MAX);
    mastr_vcat(s, spooldir, dir, BASENAME(file), NULL);
    r = rename(file, mastr_str(s));
    e = errno;
    if (r)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot move %s to %s: %m", file, dir);
    mastr_delete(s);
    errno = e;
    return r;
}

