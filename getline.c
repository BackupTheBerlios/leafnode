/*
    getline - fetch a single line from a stdio stream, arbitrary length
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

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include "critmem.h"
#include "leafnode.h"

/* Matthias Andree: implement fgets replacement that returns the number
   of characters read, excluding the terminating NUL byte. Reads at most
   size-1 bytes from stream 
 */
static ssize_t _getline(char *to, size_t size, FILE *stream)
{
    int i=0;
    int c;
    if(!size) return -1;
    size--;
    while(size-- && ((c = getc(stream)) != EOF)) {
	*to=(unsigned char)c; 
	to++;
	i++;
	if(c == '\n') break;
    }
    *to=0;
    if(ferror(stream)) return -1;
    return i;
}	

/* this is a rewritten-from-scratch glibc2 getline() replacement 
   returns characters read or -1 for EOF/error */
ssize_t getline(char **pto, size_t *size, FILE *stream) {
    ssize_t i=0, cur=0, off=0;

    if(!*size && !*pto) {
	*size=256;
	*pto=critmalloc(*size, "fgetline");
    }
    while ((cur=_getline(*pto+off, *size-off, stream)) > 0) {
	i+=cur;
	if((*pto)[i-1] == '\n') return i;
	off=i;
	*size+=*size;
	*pto=critrealloc(*pto, *size, "fgetline");
    }
    if(!cur && i) return i;
    return -1;
}

	
