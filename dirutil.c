/** \file dirutil.c
 * Operations on directories.
 * (C) 2000 - 2001 by Matthias Andree <matthias.andree@gmx.de>
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

#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include "mastring.h"
#include "leafnode.h"

#ifdef DEBUG_DMALLOC
#include <dmalloc.h>
#endif

/** open a directory below the spooldir.
 * \return DIR* handle from opendir or NULL (and set errno)
 */
DIR *
open_spooldir(const char *name /** subdirectory of spooldir to open */ )
{
    char x[PATH_MAX];

    if (snprintf(x, sizeof(x), "%s/%s", spooldir, name) >= 0) {
	return opendir(x);
    }
    errno = ENOMEM;
    return 0;
}

/** Core function to other dirlist_* functions.
 * \return malloc'd array of malloc'd strings of directory entries that 
 * passed the filter.
 * \see free_dirlist
 */
static char **
dirlist_core(
/** Directory name. */
		const char *name,
/** Filter function, it is passed a file name, if the filter returns !=
    0, the file name is inserted into the list. */
		int (*filterfunc) (const char *),
/** Flag, if set, the directory name is prefixed to file names in
    the list. */
		int prefix,
/** Flag, if set, the spooldir path is prepended to the directory name. */
		int spool,
/** Pointer to an unsigned long variable. If non-NULL, the count of
    entries in the list is stored here. */
		long unsigned *count)
{
    DIR *d;
    char **hdl, **ptr;
    size_t cms = 1024;
    struct dirent *de;
    int namelen = strlen(name);
    int snamelen = strlen(spooldir);

    d = spool ? open_spooldir(name) : opendir(name);
    if (!d)
	return 0;

    ptr = hdl = (char **)malloc(cms * sizeof(*hdl));
    if (!ptr)
	goto dirlist_nomem;	/* barf */

    while ((de = readdir(d))) {
	if (!filterfunc(de->d_name))
	    continue;
	if (ptr + 1 >= hdl + cms) {
	    int off = ptr - hdl;
	    char **nhdl;

	    *ptr = 0;		/* don't SEGV on free_dirlist in OOM condition */
	    cms += cms;
	    nhdl = (char **)realloc((char *)hdl, cms * sizeof(*hdl));
	    if (!nhdl)
		goto dirlist_nomem;
	    else
		hdl = nhdl;

	    ptr = hdl + off;
	}
	if (prefix) {
	    *ptr = (char *)malloc(namelen + 2 + strlen(de->d_name)
				  + (spool ? 1 + snamelen : 0));
	    if (*ptr) {
		char *p;
		if (spool) {
		    p = mastrcpy(*ptr, spooldir);
		    p = mastrcpy(p, "/");
		} else
		    p = *ptr;
		p = mastrcpy(p, name);
		p = mastrcpy(p, "/");
		p = mastrcpy(p, de->d_name);
	    }
	} else {
	    *ptr = strdup(de->d_name);
	}

	if (!*ptr) {
	    goto dirlist_nomem;
	}
	++ptr;
    }
    *ptr = 0;
    closedir(d);
    if (count)
	*count = ptr - hdl;
    return hdl;

  dirlist_nomem:
    if (hdl)
	free_dirlist(hdl);
    errno = ENOMEM;
    closedir(d);
    return 0;
}

/** Obtain a filtered directory list. 
 * \return malloc'd array of malloc'd strings of directory entries that 
 * passed the filter.
 * \see free_dirlist
 */
char **
dirlist(
/** Directory name. */
	   const char *name,
/** Filter function, it is passed a file name, if the filter returns !=
    0, the file name is inserted into the list. */
	   int (*filterfunc) (const char *),
/** Pointer to an unsigned long variable. If non-NULL, the count of
    entries in the list is stored here. */
	   long unsigned *count)
{
    return dirlist_core(name, filterfunc, 0, 0, count);
}

/** Obtain a filtered directory list with fully-qualified path.
 * \return malloc'd array of malloc'd strings of directory entries that 
 * passed the filter.
 * \see free_dirlist
 */
char **
dirlist_prefix(
/** Directory name. */
		  const char *name,
/** Filter function, it is passed a file name, if the filter returns !=
    0, the file name is inserted into the list. */
		  int (*filterfunc) (const char *),
/** Pointer to an unsigned long variable. If non-NULL, the count of
    entries in the list is stored here. */
		  long unsigned *count)
{
    return dirlist_core(name, filterfunc, 1, 0, count);
}

/** Obtain a filtered directory list of a directory below spooldir 
 * with fully-qualified path.
 * \return malloc'd array of malloc'd strings of directory entries that 
 * passed the filter.
 * \see free_dirlist
 */
char **
spooldirlist_prefix(
/** Directory name. */
		       const char *name,
/** Filter function, it is passed a file name, if the filter returns !=
    0, the file name is inserted into the list. */
		       int (*filterfunc) (const char *),
/** Pointer to an unsigned long variable. If non-NULL, the count of
    entries in the list is stored here. */
		       long unsigned *count)
{
    return dirlist_core(name, filterfunc, 1, 1, count);
}

/** free directory list pointed to by hdl */
void
free_dirlist(char **hdl /** string array to free */ )
{
    char **ptr = hdl;

    if (!ptr)
	return;

    while (*ptr) {
	free(*ptr);
	ptr++;
    }
    free(hdl);
}

/** Dirlist filter that lets all files pass. */
int
DIRLIST_ALL(const char *x)
{
    (void)x;			/* fix compiler warning */
    return 1;
}

/** Dirlist filter that does lets all files except dot files pass. */
int
DIRLIST_NONDOT(const char *x)
{
    return x[0] != '.';
}

/** Dirlist filter that does lets only files with all-numeric names pass. */
int
DIRLIST_ALLNUM(const char *x)
{
    char *p;
    if (!x || !*x)
	return 0;
    (void)strtoul(x, &p, 10);
    if (p && p != x && !*p)
	return 1;
    return 0;
}

#ifdef TEST
int debug = 0;

#include <stdio.h>
int
main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
	char **x = dirlist_prefix(argv[i], DIRLIST_ALLNUM, 0);
	if (x) {
	    char **y;
	    for (y = x; *y; y++) {
		puts(*y);
	    }
	    free_dirlist(x);
	}
    }
}
#endif
