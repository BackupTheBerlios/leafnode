#include "critmem.h"
#include "ln_log.h"
#include "leafnode.h"
#include "mastring.h"
#include "msgid.h"
#include "format.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

/* no good but this server isn't going to be scalable so shut up */
/*@dependent@*/ char *
lookup(/*@null@*/ const char *msgid)
{
    static /*@null@*/ /*@owned@*/ char *name = NULL;
    static unsigned int namelen = 0;
    unsigned int r;
    unsigned int i;
    char *p;
    const char *const myname = "lookup";

    if (!msgid || !*msgid)
	return NULL;

    i = strlen(msgid) + strlen(spooldir) + 30;

    if (!name) {
	name = (char *)critmalloc(i, myname);
	namelen = i;
    } else if (i > namelen) {
	name = (char *)critrealloc(name, i, myname);
	namelen = i;
    }

    p = mastrcpy(name, spooldir);
    p = mastrcpy(p, "/message.id/");
    (void)mastrcpy(p + 4, msgid);
    msgid_sanitize(p + 4);
    r = msgid_hash(p + 4);
    str_ulong0(p, r, 3);
    p[3] = '/';
    return name;
}

/** check if we already have an article with this message.id 
 \return 
   - 0 if article not present or mid parameter NULL 
   - 1 if article is already there */
/*@falsewhennull@*/ int
ihave(/*@null@*/ const char *mid
/** Message-ID of article to check, may be NULL */ )
{
    const char *m = lookup(mid);
    struct stat st;
    if (m && (0 == stat(m, &st))) {
	if (!S_ISREG(st.st_mode)) {
	    ln_log(LNLOG_SWARNING, LNLOG_CARTICLE,
		    "ihave(): article file %s is not a regular file (mode 0%o)",
		   m,  st.st_mode);
	    return 0;
	} else {
	    return 1;
	}
    }
    return 0;
}

/** atomically allocate a Message-ID unless it's already present.
 * to avoid texpire nuking the file right away, you must give another
 * file name that is linked into the Message-ID directory.
 * \return
 * - 0 for success
 * - 1 if MID already in use
 * - -1 for OS trouble.
 */
int
msgid_allocate(const char *file /** file to link into message.id */,
	const char *mid /** Non-NULL Message-ID to allocate */)
{
    const char *m = lookup(mid);
    if (sync_link(file, m) == 0) {
	return 0;
    }
    if (errno == EEXIST) return 1;
    return -1;
}

/** delete file given and the corresponding link in message.id */
int
msgid_deallocate(const char *file /** file linked into message.id */,
	const char *mid /** Non-NULL Message-ID to remove from message.id */)
{
    const char *m = lookup(mid);
    int r1 = log_unlink(file, 0);
    int r2 = log_unlink(m, 0);
    return min(r1, r2);
}
