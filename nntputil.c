/*
  nntputil -- misc nntp-related stuff. Only used by fetch.
  See AUTHORS for copyright holders and contributors.
  See README for restrictions on the use of this software.
*/
#include "leafnode.h"
#include "ln_log.h"
#include "h_error.h"
#include "attributes.h"
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <netdb.h>
#include <setjmp.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

int stat_is_evil;
long sendbuf;
extern struct serverlist *current_server;
char last_command[1025];
FILE *nntpin = NULL;
FILE *nntpout = NULL;
int authenticated;
static jmp_buf timeout;
static void
timer(int sig)
{
    (void)sig;
    longjmp(timeout, 1);
}

/*
 * Authenticate ourselves at a remote server.
 * Returns TRUE if authentication succeeds, FALSE if it does not.
 */
int
authenticate(void)
{
    int d, reply;

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
    fflush(nntpout);
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
    fflush(nntpout);
    reply = nntpreply();
    debug = d;
    if (reply != 281) {
	ln_log(LNLOG_SWARNING, LNLOG_CSERVER,
	       "authenticate: password failed: %03d", reply);
	return FALSE;
    }
    return TRUE;
}

/*
 * decode an NNTP reply number
 * reads a line from the server and returns an integer
 *
 * -1 is used to mean "protocol error"
 *
 * the text returned is discarded
 */
int
newnntpreply(char **resline)
{
    char *response;
    int r = 0;
    int c = 1;

    while (c) {
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
	    int rl;

	    rl = strtol(response, NULL, 10);
	    if (r > 0 && r != rl)
		r = -1;		/* protocol error */
	    else
		r = rl;
	    c = (response[3] == '-');
	} else {
	    c = 0;
	    r = -1;		/* protocol error */
	}
    }

    if (resline)
	*resline = response;

    return r;
}

int
nntpreply(void)
{
    return newnntpreply(0);
}

extern struct state _res;

/*
 * connect to upstream nntp server
 *
 * returns 200 for posting allowed, 201 for read-only;
 * if connection failed, return 0
 */
int
nntpconnect(const struct serverlist *upstream)
{
    struct hostent *hp;
    static struct servent *sp;
    struct servent sp_def;
    struct sockaddr_in s_in;
    int sock, reply;
    static int i;
    socklen_t optlen = sizeof(sendbuf);
    char *line;

    memset((void *)&s_in, 0, sizeof(s_in));
    if (upstream->port == 0) {
	sp = getservbyname("nntp", "tcp");
	if (sp == NULL) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "unable to find service NNTP");
	    return FALSE;
	}
    } else {
	sp = &sp_def;
	sp->s_port = htons(upstream->port);
    }
    /* Fetch the ip addresses of the given host. */
    /* FIXME: not subject to timeout control. */
    hp = gethostbyname(upstream->name);
    if (hp) {
	/* Try to make connection to each of the addresses in turn. */
	for (i = 0; (char *)(hp->h_addr_list)[i]; i++) {
	    int infd;

	    s_in.sin_family = hp->h_addrtype;
	    s_in.sin_port = sp->s_port;
	    memcpy(&s_in.sin_addr, hp->h_addr_list[i], hp->h_length);
	    sock = socket(AF_INET, SOCK_STREAM, 0);
	    /* FIXME: place IP into buffer and log */
	    if (sock < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot create socket: %m");
		break;
	    }
	    if (setjmp(timeout) != 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "timeout connecting to %s",
		       upstream->name);
		(void)close(sock);
		continue;
	    }
	    (void)signal(SIGALRM, timer);
	    (void)alarm((unsigned)upstream->timeout);
	    if (connect(sock, (struct sockaddr *)&s_in, sizeof(s_in)) < 0) {
		ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot connect to %s: %m",
		       upstream->name);
		break;
	    }
	    (void)alarm((unsigned)0);
	    nntpout = fdopen(sock, "w");
	    if (nntpout == NULL)
		break;
	    infd = dup(sock);
	    if (infd < 0) {
		ln_log(LNLOG_SERR, LNLOG_CSERVER, "cannot dup: %m");
		break;
	    }
	    nntpin = fdopen(infd, "r");
	    if (nntpin == NULL)
		break;
	    reply = newnntpreply(&line);
	    if (line && strstr(line, "NewsCache")) {
		/* NewsCache has a broken STAT implementation
		   always returns 203 0 Message-ID
		   breaking our upstream dupe detection */
		stat_is_evil = 1;
	    } else {
		stat_is_evil = 0;
	    }
	    if (reply == 200 || reply == 201) {
		/* FIXME: IPv6 */
		ln_log(LNLOG_SINFO, LNLOG_CSERVER,
		       "%s: connected to %s:%u, response: %d",
		       upstream->name,
		       inet_ntoa(s_in.sin_addr), htons(s_in.sin_port), reply);
	    }
	    if (line && strstr(line, "NNTPcache server V2.3")) {
		/* NNTPcache 2.3.3 is still in widespread use, but it
		   has Y2k bugs which have only been fixed in a beta
		   version as of 2001-12-24. This 2.3 version is
		   unsuitable for any use. */
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
	}			/* end of IP-addresses for loop */
    } else {
	ln_log(LNLOG_SERR, LNLOG_CSERVER,
	       "cannot resolve %s: %s", upstream->name, my_h_strerror(h_errno));
    }
    return 0;
}				/* end of connect function */

/*
 * disconnect from upstream server
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
