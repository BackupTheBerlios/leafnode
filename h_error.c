#include "h_error.h"
#include <netdb.h>

const char *
my_h_strerror(int he)
{
    switch (he) {
    case HOST_NOT_FOUND:
	return "no such host";
    case NO_DATA:
	return "host exists, but no such type of data";
    case NO_RECOVERY:
	return "unrecoverable server failure";
    case TRY_AGAIN:
	return "transient error, try again";
    default:
	return "unknown error";
    }
}
