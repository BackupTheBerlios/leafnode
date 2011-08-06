/** \file fopen_reg.c
 * Open a REGULAR file for reading, read-write or append.
 * Copyright (C) 2001 Matthias Andree <matthias.andree@gmx.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or(at your option) any later version.  This program is
 *  distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 *  License for more details.  You should have received a copy of the
 *  GNU Lesser General Public License along with this program; if not,
 *  write to the Free Software Foundation, Inc., 59 Temple Place, Suite
 *  330, Boston, MA 02111-1307 USA
 */

#define _POSIX_SOURCE 200112L

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "leafnode.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

/** Open an existing file if it's a regular file.
 * Sets errno in case of trouble. Safe on systems with broken stat() macros.
 * \return FILE* stream handle or NULL if it's not a regular file.
 * \bug on systems that do not support ENOTSUP, returns EINVAL instead.
 */
/*@null@*/ /*@dependent@*/ FILE *
fopen_reg(
/** file to open, parameter has the same meaning as in fopen */
	     const char *path,
/** access mode, must start with r or a, with the same meaning as in
    fopen */
	     const char *mode)
{
    FILE *f;
    struct stat st;

    if (!mode || ((mode[0] != 'r') && (mode[0] != 'a'))) {
	errno = EINVAL;
	return NULL;
    }

    f = fopen(path, mode);
    /* cannot open file */
    if (!f)
	return NULL;
    if (fstat(fileno(f), &st)) {
	/* cannot stat */
	int e = errno;

	fclose(f);
	errno = e;
	return NULL;
    }
    if (!S_ISREG(st.st_mode)) {
	fclose(f);
	errno = EINVAL;
	return NULL;
    }
    return f;
}
