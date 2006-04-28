/*
 * validatefqdn.c -- part of leafnode.
 *
 * Copyright (C) 2002 Matthias Andree <matthias.andree@gmx.de>.
 * All rights reserved.
 *

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

     * Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Matthias Andree nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
   THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/

/* This function checks that the hostname contains at least a dot, does
 * not start with localhost and does not start with 127.0.0. -- if it
 * does, syslog, print to stderr and die.
 */

#include <stdlib.h>
#include <string.h>

#include "leafnode.h"
#include "validatefqdn.h"
#include "ln_log.h"

static int strcasecmpsuffix(const char *string, const char *suffix)
{
    size_t lin = strlen(string);
    size_t lsu = strlen(suffix);

    if (lsu > lin) return -1;

    return strcasecmp(string + lin - lsu, suffix);
}

/** Check the supplied FQDN for validity.
 * \return 0 if invalid, 1 if valid
 */
int is_validfqdn(const char *f) {
    if (/* reject unqualified names */
	!strchr(f, '.')
	/* Red Hat list the FQDN on the same line as localhost, thus,
	 * the qualification returns two "localhost*" aliases */
	|| 0 == strncasecmp(f, "localhost", 9)
	/* protect against broken hosts or DNS */
	|| 0 == strcasecmp(f, "linux.local")
	/* protect against broken hosts or DNS */
	|| 0 == strncmp(fqdn, "127.", 4)
	/* kill RFC 2606 second- and top-level domains
	 * and other junk */
	|| 0 == strcasecmpsuffix(fqdn, "example.org")
	|| 0 == strcasecmpsuffix(fqdn, "example.com")
	|| 0 == strcasecmpsuffix(fqdn, "example.net")
	|| 0 == strcasecmpsuffix(fqdn, ".example")
	|| 0 == strcasecmpsuffix(fqdn, ".invalid")
	|| 0 == strcasecmpsuffix(fqdn, ".local")
	|| 0 == strcasecmpsuffix(fqdn, ".localdomain")
	|| 0 == strcasecmpsuffix(fqdn, ".localhost")
	|| 0 == strcasecmpsuffix(fqdn, ".test")
	|| 0 == strcasecmpsuffix(fqdn, ".site")
	)
    {
	return 0;
    }
    return 1;
}

void
validatefqdn(int logtostdout)
{
    /* kill bogus fqdn */
    if (!is_validfqdn(fqdn)) {
	const char *const fmt =
	    "\nLeafnode must have a fully-qualified domain name,\n"
	    "not just \"%s\".\nEdit your /etc/hosts file to add "
	    "a fully qualified domain name.\n\n";
	if (logtostdout) {
		printf("503 Leafnode must have a fully-qualified domain name. "
		       "Have its administrator fix the configuration. "
		       "More detail is in the logs.\r\n");
	}
	ln_log(LNLOG_SCRIT, LNLOG_CTOP, fmt, fqdn);	/* Flawfinder: ignore */
	fprintf(stderr, fmt, fqdn);	/* Flawfinder: ignore */
	abort();
	exit(127);
    }
}
