#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H

/* 
   attributes.h
   (C) 2000 by Matthias Andree
   LICENCE: GNU GENERAL PUBLIC LICENSE V2

   suppresses __attributes__ on function prototypes when the compiler is
   not GCC.
*/

#ifndef __GNUC__
#define __attributes__(a) 
#endif

#endif
