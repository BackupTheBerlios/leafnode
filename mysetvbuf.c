#include "mysetvbuf.h"

int
mysetvbuf(FILE * f, /*@null@*/ /*@exposed@*/ /*@out@*/ char *buf, int type, size_t size)
{
#ifdef SETVBUF_REVERSED
    return setvbuf(f, type, buf, size);
#else
    return setvbuf(f, buf, type, size);
#endif
}
