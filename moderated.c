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
 * NULL in case of error. The returned pointer must be free()ed by
 * the caller. (c) 2001 Joerg Dietrich.
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
	    char *x;
	    if ((x = strstr(p, "%s"))) {
		/* expand %s in moderator address by group name with dots
		 * replaced by dashes */
		char *g = critstrdup(group, "getmoderator");
		char *t;
		for (t = g; *t; t++)
		    if (*t == '.')
			*t = '-';

		*x = '\0';
		snprintf(address, sizeof address, "%s%s%s", p, g, x + 2);
		free(g);
	    } else {
		/* no %s? Just copy verbatim */
		(void)mastrncpy(address, p, sizeof address);
	    }
	    fclose(f);
	    mastr_delete(modpath);
	    return critstrdup(address, "getmoderator");
	}
    }
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
