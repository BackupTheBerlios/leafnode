#ifndef SYSTEM_H
#define SYSTEM_H
#include "config.h"

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

/* want uint32_t */
#if HAVE_INTTYPES_H
#include <inttypes.h>
#elif HAVE_STDINT_H
#include <stdint.h>
#elif sizeof(unsigned long) == 4 && sizeof(unsigned char) == 1
typedef unsigned long uint32_t;
typedef unsigned char uint8_t;
#else
#error "I cannot figure how to define uint32_t and uint8_t."
#endif

#endif /* SYSTEM_H */
