#ifndef MASOCK_H
#define MASOCK_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef __LCLINT__
#include <arpa/inet.h>
#endif /* not __LCLINT__ */
#include <netdb.h>

/*@null@*/ /*@only@*/ char *masock_sa2addr(const struct sockaddr *sa);
long  masock_sa2port(const struct sockaddr *sa);
/*@null@*/ /*@only@*/ char *masock_sa2name(const struct sockaddr *sa, /*@out@*/ int *h_error);

#endif
