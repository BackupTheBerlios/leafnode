#include <sys/types.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>
#include "leafnode.h"

int main(int argc, char **argv) {
    unsigned long f = 12345, l = 54321, c = 11111;
    if (argc != 2) {
	fprintf(stderr, "usage: %s dir\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    if(chdir(argv[1])) {
	perror("chdir");
	exit(EXIT_FAILURE);
    }

    if (getwatermarks(&f, &l, &c)) {
	perror("getwatermarks");
	exit(EXIT_FAILURE);
    }

    printf("first=%lu, last=%lu, count=%lu\n", f, l, c);
    exit(EXIT_SUCCESS);
}
