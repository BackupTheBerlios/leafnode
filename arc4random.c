/* $FreeBSD: src/lib/libc/gen/arc4random.c,v 1.4 2000/01/27 23:06:13 jasone Exp $ */

/*
 * Arc4 random number generator for OpenBSD.
 * Copyright 1996 David Mazieres <dm@lcs.mit.edu>.
 *
 * Modification and redistribution in source and binary forms is
 * permitted provided that due credit is given to the author and the
 * OpenBSD project (for instance by leaving this copyright notice
 * intact).
 */

/*
 * This code is derived from section 17.1 of Applied Cryptography,
 * second edition, which describes a stream cipher allegedly
 * compatible with RSA Labs "RC4" cipher (the actual description of
 * which is a trade secret).  The same algorithm is used as a stream
 * cipher called "arcfour" in Tatu Ylonen's ssh package.
 *
 * Here the stream cipher has been modified always to include the time
 * when initializing the state.  That makes it impossible to
 * regenerate the same random sequence twice, so this can't be used
 * for encryption, but will generate good random numbers.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include "arc4random.h"

struct arc4_stream {
    uint8_t i;
    uint8_t j;
    uint8_t s[256];
};

static int rs_initialized;
static struct arc4_stream rs;

static inline void
arc4_init(struct arc4_stream *as)
{
    int n;

    for (n = 0; n < 256; n++)
	as->s[n] = n;
    as->i = 0;
    as->j = 0;
}

static inline void
arc4_addrandom(struct arc4_stream *as, unsigned char * dat, int datlen)
{
    int n;
    uint8_t si;

    as->i--;
    for (n = 0; n < 256; n++) {
	as->i = (as->i + 1);
	si = as->s[as->i];
	as->j = (as->j + si + dat[n % datlen]);
	as->s[as->i] = as->s[as->j];
	as->s[as->j] = si;
    }
}

static void
arc4_stir(struct arc4_stream *as)
{
    int fd;
    struct {
	struct timeval tv;
	pid_t pid;
	uint8_t rnd[128 - sizeof(struct timeval) - sizeof(pid_t)];
    }
    rdat;

    gettimeofday(&rdat.tv, NULL);
    rdat.pid = getpid();
    fd = open("/dev/urandom", O_RDONLY, 0);
    if (fd >= 0) {
	(void)read(fd, rdat.rnd, sizeof(rdat.rnd));
	close(fd);
    }
    /* fd < 0?  Ah, what the heck. We'll just take whatever was on the
     * stack... */

    arc4_addrandom(as, (unsigned char *)&rdat, sizeof(rdat));
}

static inline uint8_t
arc4_getbyte(struct arc4_stream *as)
{
    uint8_t si, sj;

    as->i = (as->i + 1);
    si = as->s[as->i];
    as->j = (as->j + si);
    sj = as->s[as->j];
    as->s[as->i] = sj;
    as->s[as->j] = si;
    return (as->s[(si + sj) & 0xff]);
}

static inline uint32_t
arc4_getword(struct arc4_stream *as)
{
    uint32_t val;
    val = arc4_getbyte(as) << 24;
    val |= arc4_getbyte(as) << 16;
    val |= arc4_getbyte(as) << 8;
    val |= arc4_getbyte(as);
    return val;
}

void
arc4random_stir(void)
{
    if (!rs_initialized) {
	arc4_init(&rs);
	rs_initialized = 1;
    }
    arc4_stir(&rs);
}

void
arc4random_addrandom(unsigned char * dat, int datlen)
{
    if (!rs_initialized)
	arc4random_stir();
    arc4_addrandom(&rs, dat, datlen);
}

uint32_t
arc4random(void)
{
    if (!rs_initialized)
	arc4random_stir();
    return arc4_getword(&rs);
}

#if 0
/*-------- Test code for i386 --------*/
#include <stdio.h>
#include <machine/pctr.h>
int
main(int argc, char **argv)
{
    const int iter = 1000000;
    int i;
    pctrval v;

    v = rdtsc();
    for (i = 0; i < iter; i++)
	arc4random();
    v = rdtsc() - v;
    v /= iter;

    printf("%qd cycles\n", v);
}
#endif
