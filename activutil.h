#ifndef ACTIVUTIL_H
#define ACTIVUTIL_H
#include "leafnode.h"

extern ssize_t activesize;

int compactive(const void *, const void *);
void validateactive(void);
void newsgroup_copy(struct newsgroup *d, const struct newsgroup *s);
unsigned long countcaps(const char *s);
int validate_groupname(const char *name);

#endif
