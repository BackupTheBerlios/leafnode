/** queues.c
 * Manage out.going and in.coming queues.
 * \author Matthias Andree
 * \date 2001
 */

#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include "ln_log.h"
#include "leafnode.h"
#include "redblack.h"
#include "critmem.h"

/**
 * Check whether there are any articles in the queue dir.
 * Articles have names the consist entirely of digits and the minus character.
 * \return 
 *  - TRUE if there are articles
 *  - FALSE if there are no articles
 */
static int
checkqueue(
/** dir to scan (e. g. out.going, in.coming), relative to spooldir */
	      const char *const dir)
{
    DIR *d;
    struct dirent *de;

    d = open_spooldir(dir);
    if (!d) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "can't open %s: %m", dir);
	return FALSE;
    }
    while ((de = readdir(d)) != NULL) {
	/* filenames of articles to post begin with digits */
	if (check_allnum_minus(de->d_name)) {
	    closedir(d);
	    return TRUE;
	}
    }
    closedir(d);
    return FALSE;
}

/** Checks if there are postings in $(SPOOLDIR)/out.going/ 
 * \return 
 *  - TRUE if there are articles
 *  - FALSE if there are no articles
 */
int
checkforpostings(void)
{
    return checkqueue("out.going");
}

/** Checks if there are postings in $(SPOOLDIR)/in.coming/ 
 * \return 
 *  - TRUE if there are articles
 *  - FALSE if there are no articles
 */
int
checkincoming(void)
{
    return checkqueue("in.coming");
}

static int compare(const void *a, const void *b,
		   const void *config __attribute__ ((unused)));
static int
compare(const void *a, const void *b,
	const void *config __attribute__ ((unused)))
{
    return strcasecmp((const char *)a, (const char *)b);
}

/** Feeds all postings in $(SPOOLDIR)/in.coming/ into newsgroups
 * \return
 *  - TRUE in case of success
 *  - FALSE in case of trouble
 */
int
feedincoming(void)
{
    unsigned long articles;
    char **dl, **di;
    char *ni;
    struct rbtree *rb;

    rb = rbinit(compare, 0);
    if (!rb) {
	ln_log(LNLOG_SERR, LNLOG_CTOP,
	       "cannot init red-black-tree, out of memory.");
	return 0;
    }

    dl = spooldirlist_prefix("in.coming", DIRLIST_NONDOT, &articles);
    if (!dl) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read in.coming: %m");
	return 0;
    }

    ln_log(LNLOG_SINFO, LNLOG_CTOP, "found %ld article%s in in.coming.",
	   articles, articles != 1 ? "s" : "");

    for (di = dl; *di; di++) {
	FILE *f = fopen(*di, "r");
	char *ngs;
	int rc;

	if (!f) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "Cannot open %s: %m", *dl);
	    continue;
	}

	ngs = fgetheader(f, "Newsgroups:", 1);
	if (!ngs) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "Cannot read Newsgroups from %s, deleting", *dl);
	    log_unlink(*di);
	    log_fclose(f);
	    continue;
	}

	/* FIXME: fix xover */
	/* FIXME: apply filter */
	if ((rc = store(*di, 0, 0))) {
	    char buf[1024];
	    snprintf(buf, sizeof(buf), "Could not store %%s: %s",
		     store_err(rc));
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, buf, *di);
	} else {
	    const char *j;
	    for (j = strtok(ngs, "\t ,"); j && *j; j = strtok(NULL, "\t ,")) {
		const char *k = critstrdup(j, "feedincoming");
		if (!rbsearch(k, rb)) {
		    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
			   "out of memory, run texpire to fix "
			   ".overview files");
		}
	    }
	    log_unlink(*di);
	}
	log_fclose(f);
    }

    free_dirlist(dl);
    {
	RBLIST *rl = rbopenlist(rb);
	if (rl) {
	    while ((ni = rbreadlist(rl))) {
		gfixxover(ni);
		free(ni);
	    }
	    rbcloselist(rl);
	}
    }
    rbdestroy(rb);

    return 0;
}
