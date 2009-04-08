/** \file mastring.c
 * Implement auto-allocating string functions.
 */

/* (C) Copyright 2001 - 2003, 2009 by Matthias Andree <matthias.andree@gmx.de>
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
/** \addtogroup mastring
 *@{
 */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <assert.h>

#define len PRIVATE__len
#define dat PRIVATE__dat
#define bufsize PRIVATE__bufsize
#include "mastring.h"
#include "attributes.h"

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "getline.h"

static __inline__ /*@exits@*/ void
mastr_oom(void)
#ifdef MASTR_OOM_ABORT
    __attribute__ ((noreturn))
#endif
    ;

/*@noreturn@*/
static __inline__ void
mastr_oom(void)
{
#ifdef MASTR_OOM_ABORT
    abort();
#endif
}

#undef min
#define min(a,b) ((a < b) ? (a) : (b))

/** Create a new empty mastr object with an initial capacity of \a size bytes.
 \a size must be non-zero. */
mastr *
mastr_new(size_t size)
{
    mastr *n;

    assert(size != 0);

    n = (mastr *)malloc(sizeof(mastr));
    if (!n) {
	mastr_oom();
	/*@notreached@*/ return NULL;
    }
    n->bufsize = size + 1;
    if (!(n->dat = (char *)malloc(n->bufsize))) {
	free(n);
	mastr_oom();
	/*@notreached@*/ return NULL;
    }
    n->dat[0] = '\0';
    n->len = 0;
    return n;
}

/** Create a new mastr object as a copy of \a s.
    \return mastr object, or NULL if \a s is NULL or memory is insufficient. */
mastr *
mastr_newstr(const char *s)
{
    size_t l;
    mastr *n;

    if (!s)
	return NULL;
    n = mastr_new((l = strlen(s)));
    if (!n)
	return NULL;
    memcpy(n->dat, s, l + 1);
    n->len = l;
    return n;
}

/** Copy C string \a s into existing mastr object \a m, resizing it if needed.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_cpy(mastr * m, const char *s)
{
    size_t l;

    if (!m || !s)
	return 0;

    if ((l = strlen(s)) >= m->bufsize)
	if (0 == mastr_resizekill(m, l)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    memcpy(m->dat, s, l + 1);
    m->len = l;
    return 1;
}

/** Copy up to \a max characters from C string \a s into existing mastr object
 * \a m, resizing it if needed.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_ncpy(mastr * m, const char *s, size_t max)
{
    size_t l, want;

    if (!m || !s)
	return 0;

    l = strlen(s);
    want = l > max ? max : l;

    if (want >= m->bufsize)
	if (0 == mastr_resizekill(m, want)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    memcpy(m->dat, s, want);
    m->dat[want] = 0;
    m->len = l;
    return 1;
}

/** Catenate C string \a s to the end of the existing mastr object \a m,
 * resizing it if needed.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_cat(mastr * m, /*@unique@*/ /*@observer@*/ const char *const s)
{
    size_t li;

    if (!m || !s)
	return 0;

    if ((li = strlen(s)) + m->len >= m->bufsize)
	if (0 == mastr_resizekeep(m, li + m->len)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    memcpy(m->dat + m->len, s, li + 1);
    m->len += li;
    return 1;
}

/** Clear data in mastr object \a m, maintaining its current capacity. */
void
mastr_clear(mastr * m)
{
    if (!m)
	return;
    m->len = 0;
    m->dat[0] = '\0';
}

/** Catenate varargs list to end of existing mastr object \a m, resizing it as
 * needed. The argument list must end with a NULL.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_vcat(mastr * m, ...)
{
    long addlen = 0;
    const char *t;
    char *u;
    va_list v;

    if (!m)
	return 0;

    /* get length */
    va_start(v, m);
    while ((t = va_arg(v, const char *))) {
	addlen += strlen(t);
    }
    va_end(v);

    if (m->len + addlen >= m->bufsize) {
	if (!mastr_resizekeep(m, addlen + m->len)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    }
    va_start(v, m);
    u = m->dat + m->len;
    while ((t = va_arg(v, const char *))) {
	while ((*u = *t++))
	    u++;
    }
    m->len += addlen;
    va_end(v);
    return 1;
}

/** Resize existing mastr object \a m to new capacity \a l, destroying its
 * former content.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_resizekill(mastr * m, size_t l)
{
    char *n;

    if (!m)
	return 0;
    n = (char *)malloc(l + 1);
    if (!n) {
	mastr_oom();
	/*@notreached@*/ return 0;
    }
    free(m->dat);
    m->dat = n;
    m->bufsize = l + 1;
    m->dat[0] = '\0';
    m->len = 0;
    return 1;
}

/** Resize existing mastr object \a m to new capacity \a l, maintaining its
 * former content.
 * \return
 * - 1 - success
 * - 0 - failure */
int
mastr_resizekeep(mastr * m, size_t l)
{
    char *n;

    if (!m)
	return 0;
    n = (char *)realloc(m->dat, l + 1);
    if (!n) {
	free(m->dat);
	mastr_oom();
	/*@notreached@*/ return 0;
    }
    m->dat = n;
    m->bufsize = l + 1;
    m->dat[l] = '\0';
    if (l < m->len)
	m->len = l;
    return 1;
}

/** Destroy mastr object \a m, freeing its memory */
void
mastr_delete(/*@only@*/ mastr * m)
{
    if (m) {
	free(m->dat);
	free(m);
    }
}

/** Trim (delete) whitespace (as per isspace()) from the beginning of mastr
 * object \a m. */
void
mastr_triml(mastr * m)
{
    char *p, *q;
    if (!m)
	return;
    p = q = m->dat;
    while (*p && isspace((unsigned char)*p))
	p++;
    if (p != q) {
	/*@-whileempty@*/
	while ((*q++ = *p++)) { }
	/*@=whileempty@*/
	m->len -= p - q;
    }
}

/** Trim (delete) whitespace (as per isspace()) from the end of mastr
 * object \a m. */
void
mastr_trimr(mastr * m)
{
    char *p;
    if (!m)
	return;
    p = m->dat + m->len;
    /*@-whileempty@*/
    while (--p >= m->dat && isspace((unsigned char)*p)) { }
    /*@=whileempty@*/
    *++p = '\0';
    m->len = (size_t)(p - m->dat);
}

#if LEAFNODE_VERSION > 1
/** Read a line from a FILE \a f - optionally length-limited - into mastr \a m. */
ssize_t
mastr_getln(mastr * m, FILE * f,
	    ssize_t maxbytes /** if negative: unlimited */)
{
    /* FIXME: make this overflow safe, size_t vs. ssize_t. */
    char buf[4096];
    ssize_t bufsiz = (ssize_t)sizeof buf;
    ssize_t r;

    mastr_clear(m);

    for (;;) {
	r = _getline(buf,
		     (size_t)(maxbytes > 0 ? min(bufsiz, maxbytes+1) : bufsiz),
		     f);
	if (r < 0)
	    return r;
	if (r == 0)
	    break;
	if (r + m->len >= m->bufsize)
	    if (!mastr_resizekeep(m, r + m->len)) {
		mastr_oom();
		/*@notreached@*/ return 0;
	    }
	memcpy(m->dat + m->len, buf, (size_t)r); /* FIXME: avoid this copy */
	if (maxbytes > 0)
	    maxbytes -= r;
	m->len += r;
	m->dat[m->len] = '\0';
	if (memchr(buf, '\n', (size_t)r) != NULL)
	    break;
    }
    return (ssize_t)(m->len);
}

/** Perform minimal RFC-5322-style header unfolding on mastr object \a m:
    Remove LF characters that are immediately followed by whitespace. */
void mastr_unfold(mastr *m) {
    char *p, *q;
    p = q = m->dat;
    while (p < m->dat + m->len) {
	if (p[0] != '\n' || p + 1 == m->dat + m->len
		|| !isspace((unsigned char)p[1])) {
	    *q++ = *p++;
	} else {
	    p++;
	}
    }
    *q = '\0';
    m->len = q - m->dat;
}
#endif

/** Chop off last character of mastr object \a m. */
static void
choplast(mastr * m)
{
    if (m && m->len) {
	--m->len;
	m->dat[m->len] = '\0';
    }
}

/** Chop off trailing LF or CRLF from mastr object \a m*/
void
mastr_chop(mastr * m)
{
    if (m && m->len && m->dat[m->len - 1] == '\n')
	choplast(m);
    if (m && m->len && m->dat[m->len - 1] == '\r')
	choplast(m);
}

/** Return capacity of buffer currently allocated for object \a m*/
size_t
mastr_size(mastr * m)
{
    return m->bufsize - 1;
}

/** Return (used) length of string in mastr object \a m */
size_t
mastr_len(mastr * m)
{
    return m->len;
}
/*@}*/
