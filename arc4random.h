#ifndef ARC4RANDOM_H
#define ARC4RANDOM_H
extern void arc4random_stir (void);
extern void arc4random_addrandom (u_char *dat, int datlen);
extern u_int32_t arc4random (void);
#endif
