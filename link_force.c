/* link_force.c -- a C function to atomically link, replacing the destination
 * Copyright (C) 2002 Matthias Andree

     This program is free software; you can redistribute it and/or
     modify it under the terms of version 2 of the GNU General Public
     License as published by the Free Software Foundation.

     This program is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
     02111-1307  USA

     As a special exception, the "leafnode" package shall be exempt from
     clause 2b of the license (the infiltration clause).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "arc4random.h"
#include "critmem.h"
#include "link_force.h"

int link_force(const char *from, const char *to) {
    char *x = critmalloc(strlen(to)+11, "link_force");
    char *y;
    int j = 100;
    const char set[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-";

    strcpy(x, to);
    if ((y = strrchr(x, '/'))) y[1] = '\0';
    strcat(x, "_XXXXXXXXX");
    y = x + strlen(x);
    while(j) {
	int i;

	for(i = 0; i < 10 && *(--y) == 'X'; i++) {
	    *y = set[arc4random() % (strlen(set) - 1)];
	}
	if (0 == link(from, x)) {
	    int f = rename(x, to);
	    int e = errno;
	    (void)unlink(x); /* we don't care for errors here */
	    /* NOTE: SUSv2 says that if oldpath and newpath point to the
	     * same file, then rename will complete successfully without
	     * doing anything.
	     */
	    free(x);
	    errno = e;
	    return f;
	}
	if (errno != EEXIST) {
	    int e = errno;
	    free(x);
	    errno = e;
	    return -1;
	}
	while(i--)
	    *(y++) = 'X';
	--j;
    }
    free(x);
    errno = EEXIST;
    return -1;
}

#ifdef TEST
#include <stdio.h>

int main(int argc, char **argv) {
    int r, e;
    
    if (argc != 3) {
	fprintf(stderr, "usage: %s from to\n", argv[0]);
	exit(1);
    }

    r = link_force(argv[1], argv[2]);
    e = errno;
    printf("link_force(\"%s\", \"%s\") returned %d %s\n",
	    argv[1], argv[2], r, r ? strerror(e) : "");
    exit(r == 0 ? 0 : 1);
}
#endif
