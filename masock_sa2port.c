#include "masock.h"
#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <string.h>
#include <errno.h>

/** Extracts a port number from a sockaddr structure. Supports AF_INET
 * and AF_INET6. \return -1 in case of trouble, port number in host
 * order otherwise. This returns a long because unsigned short is not
 * sufficient to announce error conditions.
 */
long
masock_sa2port(const struct sockaddr *sa)
{
    switch (sa->sa_family) {
#ifdef HAVE_IPV6
    case AF_INET6:
	return ntohs(((const struct sockaddr_in6 *)sa)->sin6_port);
#endif
    case AF_INET:
	return ntohs(((const struct sockaddr_in *)sa)->sin_port);
    default:
	errno = EAFNOSUPPORT;
	return -1;
    }
}
