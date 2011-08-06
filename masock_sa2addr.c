#include "masock.h"
#include "critmem.h"
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __LCLINT__
#include <arpa/inet.h>
#endif /* not __LCLINT__ */

#include <string.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <errno.h>

/**  */
/*@null@*/ /*@only@*/ char *
masock_sa2addr(const struct sockaddr *sa)
{
    char buf[4096];
    const char *ret;

    switch (sa->sa_family) {
    case AF_INET6:
	ret = inet_ntop(sa->sa_family,
			&((const struct sockaddr_in6 *)sa)->sin6_addr,
			buf, sizeof(buf));
	break;
	/* XXX FIXME clean this up - no case needed */
    case AF_INET:
	ret = inet_ntop(sa->sa_family,
			&((const struct sockaddr_in *)sa)->sin_addr,
			buf, sizeof(buf));
	break;
    default:
	errno = EAFNOSUPPORT;
	ret = 0;
	break;
    }

    return ret ? critstrdup(ret, "masock_sa2addr") : 0;
}
