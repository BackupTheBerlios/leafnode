/** \file mastring.h
 * Implement auto-allocating string functions.
 *
 * The \b mastr object has been designed to remove some of the burden that C
 * strings entail, namely memory allocation. Internally, there is no NUL
 * termination, but a separate length field; however, for compatibility with C
 * strings, we will always allocate one more byte to be able to store a
 * trailing NUL byte.
 */

/*
 * (C) Copyright 2001 - 2002, 2009 by Matthias Andree <matthias.andree@gmx.de>
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

/** \defgroup mastring mastring - Auto-allocating string functions
 *@{*/
#ifndef MASTRING_H
#define MASTRING_H

#include "version.h"

#include <sys/types.h>		/* for size_t */
#include <stdio.h>

char *
mastrcpy(/*@out@*/ /*@returned@*/ char *dest, const char *src);

/*@null@*/ char *
mastrncpy(/*@out@*/ /*@unique@*/ /*@returned@*/ char *dest, const char *src, size_t n);

/** Basic structure of mastr object. Manipulation is only allowed for mastr_* functions, hence the PRIVATE__ prefix. */
struct mastr {
    char *PRIVATE__dat;		/**< pointer to malloc()d data */
    size_t PRIVATE__bufsize;	/**< capacity */
    size_t PRIVATE__len;	/**< used size */
};

/** Shorthand for accessing mastr objects. */
typedef struct mastr mastr;

#define MASTR_OOM_ABORT 1

/*@only@*/ mastr *mastr_new(size_t);
/*@only@*/ mastr *mastr_newstr(const char *);
int mastr_cpy(mastr *, const char *);
int mastr_ncpy(mastr *, const char *, size_t);
int mastr_cat(mastr *, /*@unique@*/ /*@observer@*/ const char *const);
int mastr_vcat(mastr *, ...);
int mastr_resizekeep(mastr *, size_t);
int mastr_resizekill(mastr *, size_t);
size_t mastr_size(mastr *);
#if LEAFNODE_VERSION > 1
ssize_t mastr_getln(mastr *, FILE *, ssize_t maxbytes);
void mastr_unfold(mastr *);
#endif
/** Shrink buffer capacity of mastr object \a m to actual used size. */
#define mastr_autosize(m) do { (void)mastr_resizekeep(m, m->len); } while(0)
void mastr_delete(/*@only@*/ mastr *);
void mastr_clear(mastr *);
void mastr_triml(mastr * m);
void mastr_trimr(mastr * m);
void mastr_chop(mastr * m);
size_t mastr_len(mastr *);

/** Trim linear whitespace from both ends of the mastr object \a m. */
#define mastr_trim(m) do { mastr_triml(m); mastr_trimr(m); } while(0)
/** Obtain const pointer to C-compatible string inside mastr object \a m. */
#define mastr_str(m) ((const char *)(m->PRIVATE__dat))
/** Obtain modifyable pointer to C-compatible string inside mastr object \a m - WARNING, you must never change the length of the string contained therein. */
#define mastr_modifyable_str(m) (m->PRIVATE__dat)
#endif
/*@}*/
