#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H
/*
   attributes.h
   (C) 2000 by Matthias Andree
   LICENCE: GNU LESSER GENERAL PUBLIC LICENSE
   suppresses __attributes__ on function prototypes when the compiler is
   not GCC.
*/
#ifndef __GNUC__
#define __attribute__(a)
#define __inline__
#endif
#endif
