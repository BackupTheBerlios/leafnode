#ifndef CONFIGPARAM_H
#define CONFIGPARAM_H

#include "config.h"

struct configparam { const char *name; int code; };

extern const struct configparam configparam[];
extern const int count_configparam;

const struct configparam *find_configparam(register const char *name);

#endif
