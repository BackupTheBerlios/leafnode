#ifndef CRITMEM_H
#define CRITMEM_H

#include <sys/types.h>

char * critmalloc(size_t size, const char* message);
char * critrealloc(char *a, size_t size, const char* message);

#endif
