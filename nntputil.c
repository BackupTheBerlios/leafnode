/**
 * nntputil.c -- miscellaneous NNTP-related stuff.
 * See AUTHORS for copyright holders and contributors.
 * See README for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "ln_log.h"
#include "h_error.h"
#include "attributes.h"
#include "masock.h"
#include "format.h"
#include "get.h"

#include <assert.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

int stat_is_evil;
long sendbuf;
extern struct serverlist *current_server;
char last_command[1025];
FILE *nntpin  = NULL;
FILE *nntpout = NULL;
int authenticated;

/**
 * Authenticate ourselves at a remote server.
 * Returns TRUE if authentication succeeds, FALSE if it does not.
 */
bool
authenticate(void)
{
    int d, reply;

    assert(nntpout != NULL);

    if (!current_server) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "authenticate: no server selected");
	return FALSE;
    }

    /* check and send username */
    if (!current_server->username) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "authenticate: %s: username needed for authentication",
	       current_server->name);
	return FALSE;
    }

    d = debug;
    debug = debugmode;
    fprintf(nntpout, "AUTHINFO USER %s\r\n", current_server->username);
    if (debug & DEBUG_NNTP)
	ln_log(LNLOG_SDEBUG, LNLOG_CSERVER,
	       ">AUTHINFO USER %s", current_server->username);
    if (fflush(nntpout)) return FALSE;
    reply = nntpreply();
    debug = d;
    if (reply == 281) {
	return TRUE;
    } else if (reply != 381) {
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "username rejected: %03d", reply);
	return FALSE;
    }

    /* check and send password */
    if (!current_server->password) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "authenticate: %s: password needed for authentication",
	       current_server->name);
	return FALSE;
    }
    debug = debugmode;
    fprintf(nntpout, "authinfo pass %s\r\n", current_server->password);
    if (debug & DEBUG_NNTP)
	ln_log(LNLOG_SDEBUG, LNLOG_CSERVER,
	       ">AUTHINFO PASS [password not shown]");
    if (fflush(nntpout)) return FALSE;
    reply = nntpreply();
    debug = d;
    if (reply != 281) {
	ln_log(LNLOG_SWARNING, LNLOG_CSERVER,
	       "authenticate: password failed: %03d", reply);
	return FALSE;
    }
    return TRUE;
}

/**
 * Reads a status line from the NNTP server, parses the status code and
 * returns it. Optionally, a pointer to the status line can be stored
 * into *resline, which is static storage.
 * \return
 * - -1 for read error
 * - status code from server otherwise.
 */
int
newnntpreply(/*@out@*/ char **resline
	     /** If non-NULL, stores pointer to line here. */)
{
    char *response;
    int r = 0;
    bool c;

    assert(nntpin != NULL);

    do {
	response = getaline(nntpin);
	if (!response) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP,
		   "NNTP server went away while waiting for response: %m");
	    return -1;
	}
	if (strlen(response) >= 3 && isdigit((unsigned char)response[0])
	    && isdigit((unsigned char)response[1])
	    && isdigit((unsigned char)response[2])
	    && ((response[3] == ' ')
		|| (response[3] == '\0')
		|| (response[3] == '-'))) {
	    long rli;
	    int rl;

	    if (0 == get_long(response, &rli) ||
		rli > INT_MAX || rli < INT_MIN)
		r = -1;
	    rl = (int)rli;
	    if (r > 0 && r != rl)
		r = -1;		/* protocol error */
	    else
		r = rl;
	    c = (response[3] == '-');
	} else {
	    c = 0;
	    r = -1;		/* protocol error */
	}
    } while(c);

    if (resline)
	*resline = response;

    return r;
}

/** Reads a line from the server, parses the status code, returns it and
 * discards the rest of the status line.
 */
int
nntpreply(void)
{
    return newnntpreply(0);
}

/** Create a socket and connect it to a remote address.
 * \returns -1 for trouble, socket descriptor if successful. 
 */
static int
any_connect(const int family, const int socktype, const int protocol,
	    const struct sockaddr *sa, socklen_t addrlen,
	    /*@exposed@*/ const char ** const errcause)
/*@modifies *errcause@ */
{
    char *as;
    int sock;

    as = masock_sa2addr(sa);
    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	   "  trying address %s port %ld...",
	   as ? as : "(unknown)", masock_sa2port(sa));

    sock = socket(family, socktype, protocol);
    if (sock < 0) {
	ln_log(LNLOG_SINFO, LNLOG_CSERVER, "  cannot create socket: %m");
	*errcause = "cannot create socket";
    } else {
	if (connect(sock, sa, addrlen) < 0) {
	    int e = errno;
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER, "  cannot connect: %m");
	    (void)close(sock);
	    *errcause = "cannot connect";
	    errno = e;
	    sock = -1;
	} else {
	    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		   "  connected: address %s port %ld.",
		   as ? as : "(unknown)", masock_sa2port(sa));
	}
    }
    if (as)
	free(as);
    return sock;
}

/** Resolve host name and connect to it, try each of its addresses in
 * turn, until a connection is established.
 * \return -1 for trouble,
 * socket descriptor otherwise.
 */
static int
tcp_connect(/** host name or address in dotted or colon (IPv6)
	     * notation */
    const char *const nodename,
    /** service name or port number */
    const char *const service,
    /** address family, 0 means "don't care" */
    int address_family)
{
    const char *errcause;
    int sock;

#ifdef HAVE_GETADDRINFO
    /* This stuff supports IPv6 and IPv4 transparently. */
    int err;

    struct addrinfo hints = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const struct addrinfo *aii;
    struct addrinfo *ai;

    hints.ai_family = address_family;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    err = getaddrinfo(nodename, service, &hints, &ai);
    if (err) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot resolve (getaddrinfo) %s:%s: %s",
	       nodename, service, gai_strerror(err));
	return -1;
    }

    sock = -1;
    errcause = "no addresses";
    errno = 0;
    for (aii = ai; aii; aii = aii->ai_next) {
	sock = any_connect(aii->ai_family, aii->ai_socktype, aii->ai_protocol,
			   aii->ai_addr, aii->ai_addrlen, &errcause);
	if (sock >= 0)
	    break;
    }
    freeaddrinfo(ai);

    if (sock < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "connecting to %s:%s: %s: %m",
	       nodename, service, errcause);
    }

    return sock;
#else
    /* This is legacy IPv4 code. */
    long port;

    struct hostent *he;
    struct sockaddr_in s_in;

    char **ha;

    if (address_family != 0 && address_family != AF_INET) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unsupported address family %d", address_family);
	return -1;
    }

    if (0 == get_long(service, &port)) {
	struct servent *se;

	se = getservbyname(service, "tcp");
	if (NULL == se) {
	    port = -1;
	} else {
	    port = ntohs(se->s_port);
	}
    } else {
	if (port > 65535)
	    port = -1;
    }
    if (port < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot get port for tcp service %s", service);
	return -1;
    }

    he = gethostbyname(nodename);
    if (!he) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot resolve (gethostbyname) %s: %s",
	       nodename, my_h_strerror(h_errno));
	return -1;
    }

    sock = -1;
    errcause = "no addresses";
    errno = 0;
    for (ha = he->h_addr_list; *ha; ha++) {
	s_in.sin_family = AF_INET;
	s_in.sin_port = htons(port);
	memcpy(&s_in.sin_addr, *ha, he->h_length);
	sock = any_connect(AF_INET, SOCK_STREAM, IPPROTO_TCP,
			   (struct sockaddr *)&s_in, sizeof(s_in),
			   &errcause);
	if (sock >= 0)
	    break;
    }
    endhostent();

    if (sock < 0) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "connecting to %s:%s: %s: %m",
	       nodename, service, errcause);
    }

    return sock;
#endif
}

/**
 * Connect to upstream NNTP server.
 *
 * \returns
 * - 200 for posting allowed
 * - 201 for read-only access
 * - 0 if the connection could not be established
 */
int
nntpconnect(const struct serverlist *upstream)
{
    int sock, reply, infd;
    socklen_t optlen = sizeof(sendbuf);
    char *line;
    char service[20];

    if (upstream->port == 0) {
	strcpy(service, "nntp");
    } else {
	str_ulong(service, upstream->port);
    }

    ln_log(LNLOG_SINFO, LNLOG_CSERVER, "%s: connecting to port %s",
	   upstream->name, service);
    sock = tcp_connect(upstream->name, service, PF_UNSPEC);
    if (sock < 0)
	return 0;

    infd = dup(sock);
    if (infd < 0) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot dup(%d): %m", sock);
	(void)close(sock);
	return 0;
    }

    nntpout = fdopen(sock, "w");
    if (nntpout == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot fdopen(%d): %m", sock);
	(void)close(sock);
	return 0;
    }

    nntpin = fdopen(infd, "r");
    if (nntpin == NULL) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot fdopen(%d): %m", infd);
	(void)fclose(nntpout);
	(void)close(sock);
	return 0;
    }

    reply = newnntpreply(&line);
    if (reply == 200 || reply == 201) {
	ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	       "%s: connected (%d), banner: \"%s\"",
	       upstream->name, reply, line ? line : "(none)");
    }

    if (line && strstr(line, "NewsCache")) {
	/* NewsCache has a broken STAT implementation
	   always returns 203 0 Message-ID
	   breaking our upstream dupe detection */
	stat_is_evil = 1;
    } else {
	stat_is_evil = 0;
    }

    if (line && strstr(line, "NNTPcache server V2.3")) {
	/* NNTPcache 2.3.3 is still in widespread use, but it
	   has Y2k bugs which have only been fixed in a beta
	   version as of 2001-12-24. This 2.3 version is
	   unsuitable for any use since 2000-01-01. */
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "%s: Server greeting \"%s\" hints to NNTPcache v2.3.x. "
	       "This server has severe Y2k bugs which make it "
	       "unsuitable for use with leafnode. "
	       "Ask the news server administrator to update.",
	       upstream->name, line);
	nntpdisconnect();
	return 0;
    }

    if (getsockopt(fileno(nntpout), SOL_SOCKET, SO_SNDBUF,
		   (char *)&sendbuf, &optlen) == -1) {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "%s: error in getsockopt: %m", upstream->name);
	nntpdisconnect();
	return 0;
    }

    ln_log(LNLOG_SINFO, LNLOG_CSERVER,
	   "%s: TCP send buffer size is %ld", upstream->name, sendbuf);
    return reply;
}

/**
 * Disconnect from upstream server.
 */
void
nntpdisconnect()
{
    if (nntpin) {
	fclose(nntpin);
	nntpin = NULL;
    }
    if (nntpout) {
	fclose(nntpout);
	nntpout = NULL;
    }
}
