#ifndef PCREWRAP_H
#define PCREWRAP_H

#include <pcre.h>

pcre *ln_pcre_compile(const char *value, int options,
	const unsigned char *tableptr, const char *filename,
	unsigned long line);
#endif
