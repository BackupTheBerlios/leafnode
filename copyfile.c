#include "leafnode.h"
#include "critmem.h"

/*
 * copy n bytes from infile to outfile
 * returns 0 for success
 * returns -1 for failure
 */
int
copyfile(FILE * infile, FILE * outfile, long n)
{
    static char *buf = NULL;
    long total;
    size_t toread, haveread, written;

    if (n == 0)
	return 0;

    if (!buf)
	buf = (char *)critmalloc(BLOCKSIZE, "Allocating buffer space");

    total = 0;
    clearerr(infile);
    clearerr(outfile);
    do {
	if ((n - total) < BLOCKSIZE)
	    toread = n - total;
	else
	    toread = BLOCKSIZE;

	haveread = fread(buf, sizeof(char), toread, infile);
	if (haveread < toread && ferror(infile)) {
	    return -1;
	}

	written = fwrite(buf, sizeof(char), haveread, outfile);
	if (written < haveread && ferror(outfile))
	    return -1;

	total += haveread;
    } while ((total < n) || (haveread < toread));
    return 0;
}
