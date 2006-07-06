/* \file getaline.c
 * Fetch a single line from a stdio stream, arbitrary length.
 * \date 2000 - 2002
 * 
 *  Copyright(C) 2000-2002,2004,2006 Matthias Andree <matthias.andree@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or(at your option) any later version.  
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License for more details.  You
 * should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA 
 */

#undef _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include "getline.h"
#include "ln_log.h"
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

static char *buf;		/* buffer for line */
static size_t size;		/* size of buffer */

/** fetch a line from file \p f and strip CRLF or LF.
 * \return pointer to static buffer holding line or 0 for I/O or OOM error.
 */
char *
getaline(FILE * f /** file to read from */ )
{
    ssize_t len;		/* # of chars stored into buf before '\0' */

    len = getline(&buf, &size, f);
    if (len < 0)
	return 0;
    if (len && (buf[len - 1] == '\n')) {	/* go back on top of
						   the newline */
	--len;
	if (len && (buf[len - 1] == '\r'))
	    /* also delete CR */
	    --len;
    } else {
	if (debugmode & DEBUG_IO) {
	    /* FIXME: CTOP? */
	    ln_log(LNLOG_SWARNING, LNLOG_CTOP, "<%s (incomplete, ignored)", buf);
	}
	return NULL;
    }
    buf[len] = '\0';		/* unconditionally terminate string,
				   possibly overwriting newline */
    if (debugmode & DEBUG_IO)
	ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "<%s", buf);	/* FIXME: CTOP? */
    return buf;
}

/**
 * Reads the next line from the buffer \p s and strips CRLF and LF.
 * \return pointer to static buffer holding line or 0 for I/O or OOM error.
 */
char *
getabufferedline(const char *s /** buffer to read from */, 
                 size_t *start, /** where in the buffer to start from */
                 size_t length /** total length of the buffer */ )
{
    size_t len;                /* # of chars stored into buf before '\0' */
    const char *t;

    if (s == NULL || start == NULL || *start >= length ) {
        return 0;
    }

    /* first have to find out how big the line will be. Counts the number of
       characters until we get to a newline */
    for (len = 0, t = s + *start; *t != '\n'; t++) { len++; }

    if (size < len) {
        /* Previous buffer was too small, realloc a bigger one. */
        if (buf) {
            free(buf);
        }
        size = len + 1;

        buf = (char *)malloc(size);
        if (buf == NULL) {
            return 0;
        }
    }

    memcpy(buf, s + *start,  len);
    buf[len] = '\0';            /* unconditionally terminate string,
                                  possibly overwriting newline */

    /* now see if there are extra chars to skip over. */
    *start += len;
    while(s[*start] == '\r') (*start)++;
    if (s[*start] == '\n') (*start)++;

    if (debugmode & DEBUG_IO)
       ln_log(LNLOG_SDEBUG, LNLOG_CTOP, "<%s", buf);   /* FIXME: CTOP? */

    return buf;
}
