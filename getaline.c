/*
    getaline - fetch a single line from a stdio stream, arbitrary length
    Copyright (C) 2000 Matthias Andree

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>

#include "getline.h"
#include "ln_log.h"
#include "leafnode.h"

char *getaline(FILE *f) {
    static char *buf;       /* buffer for line */
    static size_t size;     /* size of buffer */
    ssize_t len;            /* # of chars stored into buf before '\0' */

    len=getline(&buf, &size, f);
    if(len < 0) return 0;
    if (len && (buf[len-1] == '\n')) { /* go back on top of the newline */
	--len;
	if (len && (buf[len-1] == '\r')) /* also delete CR */
	    --len;
    }

    buf[len] = '\0';        /* unconditionally terminate string,
                               possibly overwriting newline */

    if ( debug )
        ln_log(LNLOG_DEBUG, "<%s", buf );
    return buf;
}
