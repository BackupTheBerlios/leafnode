/* mastring.h -- Implement auto-allocating string functions.
 *
 * (C) 2001 - 2002 by Matthias Andree <matthias.andree@gmx.de>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 or 2.1 of
 * the License. See the file COPYING.LGPL for details.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#ifndef MASTRING_H
#define MASTRING_H

#include <sys/types.h>		/* for size_t */
#include <stdio.h>

char *
mastrcpy(/*@out@*/ /*@returned@*/ char *dest, const char *src);
/*@null@*/ char *
mastrncpy(/*@out@*/ /*@unique@*/ /*@returned@*/ char *dest, const char *src, size_t n);

struct mastr {
    char *PRIVATE__dat;
    size_t PRIVATE__bufsize;
    size_t PRIVATE__len;
};

typedef struct mastr mastr;

#define MASTR_OOM_ABORT 1

/*@only@*/ mastr *mastr_new(size_t);
/*@only@*/ mastr *mastr_newstr(const char *);
int mastr_cpy(mastr *, const char *);
int mastr_cat(mastr *, /*@unique@*/ /*@observer@*/ const char *);
int mastr_vcat(mastr *, ...);
int mastr_resizekeep(mastr *, size_t);
int mastr_resizekill(mastr *, size_t);
size_t mastr_size(mastr *);
ssize_t mastr_getln(mastr *, FILE *, ssize_t maxbytes);
#define mastr_autosize(m) do { (void)mastr_resizekeep(m, m->len); } while(0)
void mastr_delete(/*@only@*/ mastr *);
void mastr_clear(mastr *);
void mastr_triml(mastr * m);
void mastr_trimr(mastr * m);
void mastr_chop(mastr * m);
size_t mastr_len(mastr *);
#define mastr_trim(m) do { mastr_triml(m); mastr_trimr(m); } while(0)
#define mastr_str(m) ((const char *)(m->PRIVATE__dat))
#define mastr_modifyable_str(m) (m->PRIVATE__dat)
#endif
