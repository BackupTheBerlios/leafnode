/*
libutil -- misc nntp-related stuff. Only used by fetch.

Written by Arnt Gulbrandsen <agulbra@troll.no> and copyright 1995
Troll Tech AS, Postboks 6133 Etterstad, 0602 Oslo, Norway, fax +47
22646949.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>
and Randolf Skerka <Randolf.Skerka@gmx.de>.
Copyright of the modifications 1997.
Modified by Kent Robotti <robotti@erols.com>. Copyright of the
modifications 1998.
Modified by Markus Enzenberger <enz@cip.physik.uni-muenchen.de>.
Copyright of the modifications 1998.
Modified by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
Copyright of the modifications 1998, 1999.

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
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
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

extern struct serverlist * current_server;

char last_command[1025];

FILE *nntpin = NULL;
FILE *nntpout = NULL;

int authenticated;

static jmp_buf timeout;

static void timer(int sig) {
    longjmp(timeout, 1);
    exit(sig);		/* not reached */
}

/*
05/26/97 - T. Sweeney - Send a string out, keeping a copy in reserve.
*/
void putaline( const char *fmt, ... ) {
    char lineout[1025];
    va_list args;

    va_start( args, fmt );
    vsnprintf( lineout, sizeof(lineout), fmt, args );
    if ( debug > 1 )
	syslog( LOG_DEBUG, ">%s", lineout );
    strcpy(last_command, lineout);
    fprintf(nntpout, "%s\r\n", lineout);
    fflush(nntpout);
    va_end( args );
}

/*
 * Authenticate ourselves at a remote server.
 * Returns 1 if authentication succeeds, FALSE if it does not.
 */
int authenticate( void ) {
    int d, reply;

    if ( !current_server ) {
	syslog( LOG_INFO, "authenticate: unknown server" );
	return FALSE;
    }
    if ( !current_server->username ) {
	syslog( LOG_INFO, "%s: username needed for authentication",
		current_server->name );
	return FALSE;
    }

    d = debug;
    debug = debugmode;
    fprintf( nntpout, "AUTHINFO USER %s\r\n", current_server->username );
    if ( debug )
	syslog( LOG_DEBUG, ">AUTHINFO USER [username]" );
    fflush( nntpout );

    reply = nntpreply();
    debug = d;
    if (reply == 281) {
	return 1;
    } else if ( reply != 381 ) {
	syslog( LOG_INFO, "username rejected: %03d", reply);
	return FALSE;
    }

    if ( !current_server->password ) {
	syslog( LOG_INFO, "%s: password needed for authentication",
		current_server->name );
	return FALSE;
    }
    debug = debugmode;
    fprintf( nntpout, "authinfo pass %s\r\n", current_server->password );
    if ( debug )
	syslog( LOG_DEBUG, ">AUTHINFO PASS [password]" );
    fflush( nntpout );

    reply = nntpreply();
    debug = d;

    if ( reply != 281) {
	syslog( LOG_INFO, "password failed: %03d", reply);
	return FALSE;
    }
    return 1;
}


/*
 * decode an NNTP reply number
 * reads a line from the server and returns an integer
 *
 * 498 is used to mean "protocol error", like smail
 *
 * the text returned is discarded
 *
 * from Tim Sweeney: retry in case of authinfo failure.
 */
int nntpreply(void) {
    char *response;
    int r = 0;
    int c = 1;

    while (c) {
	response=getaline(nntpin);
	if (!response) {
	    syslog( LOG_ERR, "NNTP server went away" );
	    return 498;
	}
	if (strlen(response)>2
	    && isdigit((unsigned char)response[0])
	    && isdigit((unsigned char)response[1])
	    && isdigit((unsigned char)response[2])
	    && ( (response[3]==' ')
		 || (response[3]=='\0')
		 || (response[3]=='-') ) ) {
	    int rl;
	    rl = strtol( response, NULL, 10 );
	    if (r>0 && r!=rl)
		r = 498;    /* protocol error */
	    else
		r = rl;
	    c = (response[3]=='-');
	} else {
	    c = 0;
	    r = 498;	/* protocol error */
	}
    }

    if (r == 480 && !authenticated ) { /* need to authenticate */
	authenticated = 1;
	if ( authenticate() ) {
	    fprintf(nntpout, "%s\r\n", last_command);
	    fflush(nntpout);
	    r = nntpreply();
	}
    }
    return r;
}


extern struct state _res;

#define incopy(a)       (*((struct in_addr *)a))

/*
 * connect to upstream nntp server
 *
 * returns 200 for posting allowed, 201 for read-only;
 * if connection failed, return 0
 */
int nntpconnect( const struct serverlist * upstream ) {
    struct hostent *hp;
    static struct servent *sp;
    struct servent sp_def;
    struct sockaddr_in s_in;
    int sock, reply;
    static int i;

    memset((void *)&s_in, 0, sizeof(s_in));
    if (upstream->port == 0) {
	sp = getservbyname("nntp", "tcp");
	if (sp == NULL) {
	    syslog( LOG_ERR, "unable to find service NNTP" );
	    return FALSE;
	}
    } else {
	sp=&sp_def;
	sp->s_port=htons(upstream->port);
    }

    /* Fetch the ip addresses of the given host. */
    hp = gethostbyname( upstream->name );
    if (hp) {

	/* Try to make connection to each of the addresses in turn. */
	for (i = 0; (int *)(hp->h_addr_list)[i]; i++) {
	    s_in.sin_family = hp->h_addrtype;
	    s_in.sin_port = sp->s_port;
	    s_in.sin_addr = incopy(hp->h_addr_list[i]);

	    sock = socket(AF_INET, SOCK_STREAM, 0);
	    if (sock < 0)
		break;

	    if (setjmp(timeout) != 0) {
		(void) close(sock);
		continue;
	    }

	    (void) signal(SIGALRM, timer);
	    (void) alarm((unsigned) upstream->timeout );
	    if (connect(sock, (struct sockaddr *)&s_in, sizeof(s_in)) < 0)
		break;
	    (void) alarm((unsigned)0);

	    nntpout = fdopen(sock, "w");
	    if (nntpout == NULL)
		break;

	    nntpin  = fdopen(dup(sock), "r");
	    if (nntpin == NULL)
		break;

            reply = nntpreply();
	    if ( reply == 200 || reply == 201 ) {
		syslog( LOG_INFO, "connected to %s: %d",
			inet_ntoa( s_in.sin_addr ), reply );
		return reply;
	    }
	    shutdown(fileno(nntpout), 0);
	}/* end of IP-addresses for loop */
    }
    return FALSE;
}/* end of connect function */

/*
 * disconnect from upstream server
 */
void nntpdisconnect( void ) {
    if ( nntpin ) {
	fclose( nntpin );
	nntpin = NULL ;
    }
    if ( nntpout ) {
	fclose( nntpout );
	nntpout = NULL ;
    }
}
