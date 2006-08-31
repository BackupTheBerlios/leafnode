#include "leafnode.h"
#include "ln_log.h"
#include "mastring.h"
#include "critmem.h"

#include <assert.h>
#include <ctype.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include <string.h>

/**
 * Takes a group name as argument, returns the moderator's address,
 * NULL in case of error or no moderator. The returned pointer must be free()ed by
 * the caller. (c) 2001 Joerg Dietrich, 2006 Matthias Andree.
 */
/*@null@*/ /*@only@*/ char *
getmoderator(const char *group)
{
    char *line, *p;
    char address[512];
    mastr *modpath = mastr_new(LN_PATH_MAX);
    FILE *f;

    mastr_vcat(modpath, sysconfdir, "/moderators", NULL);
    f = fopen(mastr_str(modpath), "r");
    if (!f) {
	ln_log(LNLOG_SNOTICE, LNLOG_CTOP, "Could not open %s: %m",
	       mastr_str(modpath));
	mastr_delete(modpath);
	return NULL;
    }
    while ((line = getaline(f))) {
	if (!*line || line[0] == '#')
	    continue;
	if (!(p = strchr(line, ':'))) {
	    /* invalid line */
	    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		   "warning: moderator line \"%s\" malformatted, skipping",
		   line);
	    continue;
	}
	*p++ = '\0';
	if (wildmat(group, line)) {
	    char *x, *y;
	    if (p[0] == '\0') {
		/* empty moderator address means: post instead of
		 * mailing and leave handling to upstream */
		goto nomod;
	    }
	    for (x = p, y = address; *x; x++) {
		if (y + 1 >= address + sizeof(address)) {
overrun:
		    ln_log(LNLOG_SERR, LNLOG_CTOP, "Buffer overrun while determining moderator. Cowardly pretending there were no moderator.");
		    goto nomod;
		}
		if (*x != '%') {
		    *y++ = *x;
		} else {
		    if (x[1] == '%') {
			*y++ = *x++;
		    } else if (x[1] == 's') {
			/* expand %s in moderator address by group name with dots
			 * replaced by dashes */
			char *g, *t;
			++x;
			if (y + strlen(group) >= address + sizeof(address))
			    goto overrun;
			g = critstrdup(group, "getmoderator");
			for (t = g; *t; t++)
			    if (*t == '.')
				*t = '-';

			memcpy(y, g, strlen(g));
			y += strlen(g);
			free(g);
		    } else {
			*y++ = *x;
		    }
		}
	    }
	    *y = '\0';
	    fclose(f);
	    mastr_delete(modpath);
	    return critstrdup(address, "getmoderator");
	}
    }
nomod: /* no moderator or error */
    fclose(f);
    mastr_delete(modpath);
    return NULL;
}

/**
 * Takes a comma seperated list of newsgroups and returns the
 * the first group on which status matches, NULL otherwise.
 * Caller must free returned pointer. (c) 2001 Joerg Dietrich.
 */
/*@null@*/ /*@only@*/ char *
checkstatus(const char *groups, const char status)
{
    char *p;
    /*@dependent@*/ char *q;
    /*@dependent@*/ char *grp;
    struct newsgroup *g;

    assert(groups != NULL);
    grp = p = critstrdup(groups, "checkstatus");

    SKIPLWS(grp);
    while (grp && *grp) {
	q = strchr(grp, ',');
	if (q) {
	    *q++ = '\0';
	    SKIPLWS(q);
	}

	g = findgroup(grp, active, -1);
	if (g) {
	    if (g->status == status) {
		free(p);
		return critstrdup(g->name, "checkstatus");
	    }
	}
	grp = q;
	if (grp)
	    SKIPLWS(grp);
    }
    free(p);
    return NULL;
}

#ifdef TEST
int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; i++) {
	char *x = getmoderator(argv[i]);
	printf("moderator for %s: %s\n", argv[i], x ? x : "none");
	if (x) free(x);
    }
    return 0;
}
#endif
