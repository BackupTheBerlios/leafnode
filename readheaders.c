#include "leafnode.h"
#include "critmem.h"

#define MAXHEADERSIZE 16383

/* read from fd into malloced buffer *bufp of size *size
 * up to delim or EOF.  Buffer is adjusted to fit input.
 * return pointer to match or end of file, NULL in case of error
 */
/*@null@*/ /*@dependent@*/ static char *
readtodelim(int fd, const char *name, /*@unique@*/ /*@observer@*/ const char *delim,
    char **bufp, size_t *size)
{
    size_t dlen = strlen(delim) - 1;
    size_t nread;
    ssize_t res;
    char *k;

    nread = 0;
    if (*size < 1 || *bufp == NULL)
	*bufp = (char *)critmalloc((*size = MAXHEADERSIZE+1), "readtodelim");

    /*@+loopexec@*/
    for (;;) {
	res = read(fd, *bufp + nread, *size - nread - 1);
	if (res < 0) { /* read error/EINTR/whatever */
	    printf("error reading %s: %s\n", name, strerror(errno));
	    return NULL;
	}

	(*bufp)[nread + res] = '\0';
	/* skip as much as possible */
	k = strstr(nread > dlen ? *bufp + nread - dlen : *bufp, delim);

	nread += res;
	if ((size_t)res < *size-nread-1) { /* FIXME: can short reads happen? */
	    return k != NULL ? k : *bufp + nread;
	}

	if (k != NULL) {
	    return k;
	}

	/* must read more */
	*bufp = (char *)critrealloc(*bufp, (*size)*=2, "readtodelim");
    }
    /*@=loopexec@*/
}

/* read article headers, cut off body
 * return 0 for success, -1 for error, -2 for article without body
 */
int
readheaders(int fd, /*@unique@*/ const char *name, char **bufp, size_t *size, const char *delim)
{
    char *k = readtodelim(fd, name, delim, bufp, size); /* XXX FIXME:
							    cater for wire
							    format */
    if (k != NULL) {
	if (*k == '\0')
	    return -2;
	else {
	    k[1] = '\0'; /* keep last header line \n terminated */
	    return 0;
	}
    } else {
	return -1;
    }
}
