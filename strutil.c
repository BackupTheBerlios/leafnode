/* strutil.c
  
   (C) 2000 by Matthias Andree
   GNU GPL 2
*/

#include <string.h>
#include "leafnode.h"

int check_allnum(const char *x) {
    while(*x) {
	if(!strchr("0123456789", *x)) return 0;
	x++;
    }
    return 1;
}
