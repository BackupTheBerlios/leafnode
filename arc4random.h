#ifndef ARC4RANDOM_H
#define ARC4RANDOM_H

#include "system.h"

extern void arc4random_stir (void);
extern void arc4random_addrandom (unsigned char *dat, int datlen);
extern uint32_t arc4random (void);

#endif
