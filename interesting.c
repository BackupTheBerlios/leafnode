#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include "leafnode.h"
#include "mastring.h"
#include "ln_log.h"
#include "critmem.h"
#include "redblack.h"

static /*@null@*/ /*@only@*/ struct rbtree *rb_interesting;

/**
 * Check when the last posting has arrived in the group. \return 0 in
 * case of trouble, st_ctime of LASTPOSTED file of that group otherwise.
 */
static time_t
getlastart(const char *group) /*@globals errno, spooldir;@*/ ;
static time_t
getlastart(const char *group)
  /*@globals errno, spooldir;@*/
{
    mastr *g;
    char *p = critstrdup(group, "getlastart"), *q;
    struct stat st;
    time_t ret = 0;

    g = mastr_new(LN_PATH_MAX);
    for (q = p; *q; q++)
	if (*q == '.')
	    *q = '/';
	else
	    *q = tolower((unsigned char)*q);
    mastr_vcat(g, spooldir, "/", p, "/", LASTPOSTING, NULL);
    free(p);
    if (stat(mastr_str(g), &st)) {
	if (errno != ENOENT) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot stat %s: %m", mastr_str(g));
	}
    } else {
	ret = st.st_ctime;
    }
    mastr_delete(g);
    return ret;
}

static void
killlastposting(const char *group)
/*@globals errno, spooldir;@*/
{
    mastr *g;
    char *p = critstrdup(group, "killlastposting"), *q;

    g = mastr_new(LN_PATH_MAX);
    for (q = p; *q; q++)
	if (*q == '.')
	    *q = '/';
	else
	    *q = tolower((unsigned char)*q);

    mastr_vcat(g, spooldir, "/", p, "/", LASTPOSTING, NULL);
    free(p);
    if (unlink(mastr_str(g))) {
	if (errno != ENOENT) {
	    ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot unlink %s: %m", mastr_str(g));
	}
    }
    mastr_delete(g);
}

/**
 * Check which groups are still interesting and unsubscribe groups
 * which are not.
 */
void
expireinteresting(void)
{
    time_t now;
    struct dirent *de;
    struct stat st;
    DIR *d;

    ln_log(LNLOG_SDEBUG, LNLOG_CGROUP, "expiring interesting.groups");

    now = time(NULL);
    if (chdir(spooldir) || chdir("interesting.groups")) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unable to chdir to %s/interesting.groups: %m", spooldir);
	return;
    }
    d = opendir(".");
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "unable to opendir %s/interesting.groups: %m", spooldir);
	return;
    }
    while ((de = readdir(d)) != NULL) {
	time_t lastart = getlastart(de->d_name);
	if ((stat(de->d_name, &st) == 0) && S_ISREG(st.st_mode)) {
	    /*@dependent@*//*@null@*/ const char *reason;
	    /* reading a newsgroup changes the ctime (in
	     * markinterest()), if the newsgroup is newly created, the
	     * mtime is changed as well */
	    /* timeout_long for read groups */
	    if ((lastart - st.st_ctime > (timeout_long * SECONDS_PER_DAY))) {
		reason =
		    "established group not read for \"timeout_long\" days after last article";
	    }
	    /* timeout_short for unread groups */
	    else if ((st.st_mtime == st.st_ctime) /* unread so far */ &&
		     (lastart - st.st_ctime >
		      (timeout_short * SECONDS_PER_DAY))) {
		reason =
		    "newly-arrived group not read for \"timeout_short\" days after last article";
	    } else
		reason = NULL;
	    if (reason) {
		ln_log(LNLOG_SINFO, LNLOG_CGROUP,
		       "skipping %s from now on: %s\n", de->d_name, reason);
		killlastposting(de->d_name);
		(void)log_unlink(de->d_name);
		/* don't reset group counts because if a group is
		   resubscribed, new articles will not be shown */
	    }
	}
    }
    (void)closedir(d);
}

/**
 * check whether "groupname" is represented in interesting.groups, without
 * touching the file.
 */
int
is_interesting(const char *groupname)
{
    /* Local groups are always interesting. At least for the server :-) */
    if (is_localgroup(groupname))
	return TRUE;

    if (!initinteresting())
	exit(EXIT_FAILURE);
    /* dormant groups are not interesting */

    return rbfind(groupname, rb_interesting) != 0;
}

/** Read interesting.groups directory. Trouble is logged.
 *  \return TRUE for success, FALSE in case of trouble.
 */
int
initinteresting(void)
{
    if (rb_interesting == NULL)
	rb_interesting = initgrouplistdir("/interesting.groups");
    if (rb_interesting)
	return TRUE;
    else
	return FALSE;
}

/*@null@*/ /*@only@*/ RBLIST *
openinteresting(void)
{
    return rbopenlist(rb_interesting);
}

/*@null@*/ /*@owned@*/ const char *
readinteresting(/*@null@*/ RBLIST * r)
{
    return (const char *)rbreadlist(r);
}

/*@null@*/ /*@owned@*/ const char *
addtointeresting(const char *key)
{
    return rbsearch(key, rb_interesting);
}

void
closeinteresting(/*@null@*/ /*@only@*/ RBLIST * r)
{
    rbcloselist(r);
}

void
freeinteresting(void)
{
    if (rb_interesting) {
	freegrouplist(rb_interesting);
	rb_interesting = NULL;
    }
}
