#ifndef CONFIGPARAM_H
#define CONFIGPARAM_H

#include "config.h"

struct configparam { const char *name; int code; };

const struct configparam *find_configparam(
    register const char *name, 
    register 
#ifdef GPERF_UNSIGNED
    unsigned
#endif
    int len 
);

#endif
