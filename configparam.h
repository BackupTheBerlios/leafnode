#ifndef CONFIGPARAM_H
#define CONFIGPARAM_H
#include "config.h"
struct configparam {
    const char *name;
    int code;
    int scope;
};

/* third column of config.table */
enum { CS_GLOBAL = 4711, CS_SERVER, CS_SERVERDECL };
/* CS_GLOBAL: global configuration parameter
 * CS_SERVER: server-specific configuration parameter
 * CS_SERVERDECL: parameter starts new server configuration 
 */

extern const struct configparam configparam[];
extern const int count_configparam;
const struct configparam *find_configparam(register const char *name);
#endif
