/*
 * debugutil.c -- part of leafnode.
 *
 * Copyright (C) 2002 Matthias Andree <matthias.andree@gmx.de>.
 * All rights reserved.
 *
 */

#include "leafnode.h"
#include <stdlib.h>
#include <signal.h>

/** d_stop_mid - raise a SIGSTOP if the passed-in Message-ID matches
 * that of the environment variable STOP_AT_MESSAGEID. */
void d_stop_mid(const char *mid) {
    static const char name[] = "STOP_AT_MESSAGEID";
    if (getenv(name)
	&& 0 == strcasecmp(mid, getenv(name)))
    {
	raise(SIGSTOP);
    }
}
