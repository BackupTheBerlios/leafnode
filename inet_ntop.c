/**
 * \file inet_ntop
 * inet_ntop emulation based on inet_ntoa.
 * \author Matthias Andree
 * 
 */
#include "config.h"
#ifndef HAVE_INET_NTOP
#include "leafnode.h"
#include "mastring.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <string.h>

const char *
inet_ntop(int af, const void *s, char *dst, int x)
{
    switch (af) {
    case AF_INET:
	mastrncpy(dst, inet_ntoa(*(const struct in_addr *)s), x);
	return dst;
	break;
    default:
	errno = EINVAL;
	return 0;
	break;
    }
}
#endif
