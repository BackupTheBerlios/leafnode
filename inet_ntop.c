/**
 * \file inet_ntop.c
 * inet_ntop emulation based on inet_ntoa.
 * \author Matthias Andree
 * 
 */
#include "config.h"
#if !HAVE_INET_NTOP
#include "leafnode.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include <string.h>

const char *
inet_ntop(int af, const void *s, char *dst, int x)
{
    switch (af) {
    case AF_INET:
	strncpy(dst, inet_ntoa(*(const struct in_addr *)s), x);
	if (x) dst[x-1] = '\0';
	return dst;
	break;
    default:
	errno = EINVAL;
	return 0;
	break;
    }
}
#endif
