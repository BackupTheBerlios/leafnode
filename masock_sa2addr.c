#include "masock.h"
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
char *
masock_sa2addr(const struct sockaddr *sa)
{
#if defined(HAVE_IPV6) || defined(HAVE_INET_NTOP)
    char buf[4096];
#endif
    const char *ret;

    switch (sa->sa_family) {
#ifdef HAVE_IPV6
    case AF_INET6:
	ret = inet_ntop(sa->sa_family,
			&((const struct sockaddr_in6 *)sa)->sin6_addr,
			buf, sizeof(buf));
	break;
#endif
    case AF_INET:
#ifdef HAVE_INET_NTOP
	ret = inet_ntop(sa->sa_family,
			&((const struct sockaddr_in *)sa)->sin_addr,
			buf, sizeof(buf));
#else
	ret = inet_ntoa(((const struct sockaddr_in *)sa)->sin_addr);
#endif
	break;
    default:
	errno = EAFNOSUPPORT;
	ret = 0;
	break;
    }

    return ret ? strdup(ret) : 0;
}
