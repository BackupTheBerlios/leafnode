/*
     grouplist.c -- determine list of all groups in the spool.
     Copyright (C) 2002  Matthias Andree

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "grouplist.h"
#include "mastring.h"

#include <unistd.h>
#include <sys/types.h>
#include "system.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#define NDEBUG 1
#include <assert.h>

#ifdef TEST
#include <stdio.h>
#else
#include "ln_log.h"
#endif

static int myscandir(struct stringlisthead **l, const char *prefix, int toplevel)
{
    DIR *d = opendir(".");
    struct dirent *de;
    int added = 0, founddir = 0;

    assert(l);
    assert(prefix);

    initlist(l);

    if (d == NULL) {
#ifdef TEST
	fprintf(stderr, "cannot open directory: %s, see next error\n", strerror(errno));
#else
	ln_log(LNLOG_SERR, LNLOG_CTOP,
		"cannot open directory: %m, see next error");
#endif
	return -1;
    }
    while ((de = readdir(d)) != NULL) {
	struct stat st;
	mastr *p;

	if (strcmp(de->d_name, ".overview") != 0
	    && strchr(de->d_name, '.') != NULL) {
	    continue;
	}
	if (toplevel && strcmp(de->d_name, "lost+found") == 0) {
	    continue;
	}
	p = mastr_new(4096);
	if (lstat(de->d_name, &st)) goto bail;
	if (added == 0 && S_ISREG(st.st_mode)) {
	    added = 1;
	    prependtolist(*l, prefix);
	} else if (S_ISDIR(st.st_mode)) {
	    founddir = 1;
	    if (prefix != NULL && *prefix != '\0')
		(void)mastr_vcat(p, prefix, ".", de->d_name, NULL);
	    else
		(void)mastr_cpy(p, de->d_name);
	    if (chdir(de->d_name) == 0) {
		if (myscandir(l, mastr_str(p), 0)) {
#ifdef TEST
		    fprintf(stderr, "myscandir(\"%s\") failed.\n", de->d_name);
#else
		    ln_log(LNLOG_SERR, LNLOG_CTOP, "myscandir(\"%s\") failed.", de->d_name);
#endif
		    goto bail;
		}
		if (chdir("..")) goto bail;
	    } else {
#ifdef TEST
		fprintf(stderr, "chdir(\"%s\") failed: %s\n", de->d_name, strerror(errno));
#else
		ln_log(LNLOG_SERR, LNLOG_CTOP, "chdir(\"%s\") failed: %m", de->d_name);
#endif
	    }
	    mastr_delete(p);
	    continue;
	bail:
	    mastr_delete(p);
	    (void)closedir(d);
	    return -1;
	}
	mastr_delete(p);
    }
    (void)closedir(d);
    if (!added && !founddir) {
	prependtolist(*l, prefix);
    }
    return 0;
}

/*@null@*/ struct stringlisthead *
get_grouplist(void)
{
    struct stringlisthead *g = NULL;

    if (chdir(spooldir)) return NULL;
    if (myscandir(&g, "", 1) != 0 && g != NULL) {
	freelist(g);
	g = NULL;
    }
    return g;
}

#ifdef TEST
int debug=0;

int main(int argc, char **argv) {
    struct stringlisthead *l;
    struct stringlistnode *n;

    if (argc > 1) { spooldir = argv[1]; }
    l = get_grouplist();
    if (!l)
	fputs("failed\n", stderr);
    else {
	for (n = l->head; n->next; n=n->next)
	    puts(n->string);
	freelist(l);
    }
    return 0;
}
#endif
