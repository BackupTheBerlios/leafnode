/** \file mastring.c -- Implement auto-allocating string functions.
 * \author Matthias Andree
 * \year 2001
 */

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <assert.h>

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

mastr *
mastr_new(size_t size)
{
    mastr *n = malloc(sizeof(mastr));

    assert(size != 0);
    if (!n) {
	mastr_oom();
	/*@notreached@*/ return NULL;
    }
    n->bufsize = size + 1;
    if (!(n->dat = malloc(n->bufsize))) {
	free(n);
	mastr_oom();
	/*@notreached@*/ return NULL;
    }
    n->dat[0] = '\0';
    n->len = 0;
    return n;
}


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
    strcpy(n->dat, s);
    n->len = l;
    return n;
}

int
mastr_cpy(mastr * m, const char *s)
{
    size_t l = strlen(s);

    if (!m)
	return 0;
    if (!s)
	return 0;
    if (l >= m->bufsize)
	if (0 == mastr_resizekill(m, l)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    strcpy(m->dat, s);
    m->len = l;
    return 1;
}

int
mastr_cat(mastr * m, /*@observer@*/ const char *const s)
{
    size_t li = strlen(s);

    if (!m)
	return 0;
    if (!s)
	return 0;
    if (li + m->len >= m->bufsize)
	if (0 == mastr_resizekeep(m, li + m->len)) {
	    mastr_oom();
	    /*@notreached@*/ return 0;
	}
    strcpy(m->dat + m->len, s);
    m->len += li;
    return 1;
}

void
mastr_clear(mastr * m)
{
    if (!m)
	return;
    m->len = 0;
    m->dat[0] = '\0';
}

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

int
mastr_resizekill(mastr * m, size_t l)
{
    char *n;

    if (!m)
	return 0;
    n = malloc(l + 1);
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

int
mastr_resizekeep(mastr * m, size_t l)
{
    char *n;

    if (!m)
	return 0;
    n = realloc(m->dat, l + 1);
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

void
mastr_delete(/*@only@*/ mastr * m)
{
    if (m) {
	free(m->dat);
	free(m);
    }
}

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
	while ((*q++ = *p++));
	/*@=whileempty@*/
	m->len -= p - q;
    }
}

void
mastr_trimr(mastr * m)
{
    char *p;
    if (!m)
	return;
    p = m->dat + m->len;
    /*@-whileempty@*/
    while (--p >= m->dat && isspace((unsigned char)*p));
    /*@=whileempty@*/
    *++p = '\0';
    m->len = (size_t)(p - m->dat);
}

ssize_t
mastr_getln(mastr * m, FILE * f,
	    ssize_t maxbytes /** if negative: unlimited */ )
{
    /* FIXME: make this overflow safe, size_t vs. ssize_t. */
    char buf[40];
    ssize_t bufsiz = (ssize_t)sizeof buf;
    ssize_t r;
    char *c;

    mastr_clear(m);

    for (;;) {
	r = _getline(buf,
		     (size_t)(maxbytes > 0 ? min(bufsiz, maxbytes) : bufsiz),
		     f);
	if (r < 0)
	    return r;
	if (r + m->len >= m->bufsize)
	    if (!mastr_resizekeep(m, r + m->len)) {
		mastr_oom();
		/*@notreached@*/ return 0;
	    }
	memcpy(m->dat + m->len, buf, (size_t)r);
	if (maxbytes > 0)
	    maxbytes -= r;
	m->len += r;
	if (r == 0 || (c = memchr(buf, '\n', (size_t)r)))
	    break;
    }
    return (ssize_t)(m->len);
}

/* chop off last character of string */
static void
choplast(mastr * m)
{
    if (m && m->len) {
	--m->len;
	m->dat[m->len] = '\0';
    }
}

/** chop off trailing LF or CRLF */
void
mastr_chop(mastr * m)
{
    if (m && m->len && m->dat[m->len - 1] == '\n')
	choplast(m);
    if (m && m->len && m->dat[m->len - 1] == '\r')
	choplast(m);
}
