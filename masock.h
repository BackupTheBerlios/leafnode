#ifndef MASOCK_H
#define MASOCK_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

char *masock_sa2addr(const struct sockaddr *sa);
char *masock_sa2name(const struct sockaddr *sa, int *h_error);

#endif
