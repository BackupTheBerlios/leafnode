#ifndef ACTIVUTIL_H
#define ACTIVUTIL_H
#include "leafnode.h"

extern size_t activesize;

int compactive(const void *, const void *);
void validateactive(void);
void newsgroup_copy(struct newsgroup *d, const struct newsgroup *s);
unsigned long countcaps(const char *s);

#endif
