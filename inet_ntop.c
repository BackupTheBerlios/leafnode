#include "leafnode.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#ifndef HAVE_INET_NTOP
const char *inet_ntop(int af, const void *s, char *dst, int x) {
    switch(af) {
	case AF_INET:
	    strncpy(dst, inet_ntoa(*(struct in_addr *)s), x);
	    dst[x]=0;
	    return dst;
	    break;
	default:
	    errno=EINVAL;
	    return 0;
	    break;
    }
}  

#endif
