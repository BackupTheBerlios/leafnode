/** \file masock_sa2name.c
 *  (C) Copyright 2001,2013 by Matthias Andree
 *  \author Matthias Andree
 *  \year 2001,2013
 */

#define _GNU_SOURCE	// to expose nonstandard members in in.h structures to
			// fix compilation in strict conformance mode on Linux

#include "masock.h"
#include "critmem.h"
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __LCLINT__
#include <arpa/inet.h>
#endif /* not __LCLINT__ */
#include <netdb.h>

#include <string.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <errno.h>

/** Look up the host name belonging to the socket address. If compiled
 * with IPv6 support, use IPv6 lookup unless the address is actually an
 * IPv6-mapped IPv4 address.
 *
 * \return strdup'ed string containing the host name or NULL in case of
 * trouble.
 *
 * \bug Linux [e]glibc has broken IN6_IS_ADDR_* macros that do not cast to
 * const and that do not work in strict conformance mode because they
 * access nonstandard members.
 */
char *
masock_sa2name(const struct sockaddr *sa
    /** socket address to convert */ ,
	       /*@out@*/ int *h_e
    /** variable to place h_errno into */ )
{
    struct hostent *he;
    char *ret;

    switch (sa->sa_family) {
#ifdef HAVE_IPV6
    case AF_INET6:
	/* any warnings issued here are the fault of broken system
	   includes */
	if (IN6_IS_ADDR_V4MAPPED(&((const struct sockaddr_in6 *)sa)->sin6_addr)) {
	    /* do IPv4 lookup on mapped addresses */
	    /* see RFC-2553 section 6.2 for "12" */
	    he = gethostbyaddr(12 + (const char *)
			       &((const struct sockaddr_in6 *)sa)
			       ->sin6_addr, sizeof(struct in_addr), AF_INET);
	} else {
	    /* real IPv6 */
	    he = gethostbyaddr((const char *)
			       &((const struct sockaddr_in6 *)sa)
			       ->sin6_addr,
			       sizeof(struct in6_addr), sa->sa_family);
	}
	if (!he)
	    errno = 0, *h_e = h_errno;
	break;
#endif
    case AF_INET:
	he = gethostbyaddr((const char *)
			   &((const struct sockaddr_in *)sa)
			   ->sin_addr.s_addr,
			   sizeof(struct in_addr), sa->sa_family);
	if (!he)
	    errno = 0, *h_e = h_errno;
	break;
    default:
	errno = EAFNOSUPPORT;
	*h_e = 0;
	he = 0;
	break;
    }

    ret = he && he->h_name ? critstrdup(he->h_name, "masock_sa2name") : 0;
    endhostent();
    return ret;
}
