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

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#include "critmem.h"
#include "mastring.h"

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

    dl = spooldirlist_prefix("in.coming", DIRLIST_NONDOT, &articles);
    if (!dl) {
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot read in.coming: %m");
	return 0;
    }

    ln_log(LNLOG_SINFO, LNLOG_CTOP, "found %lu article%s in in.coming.",
	   articles, articles != 1 ? "s" : "");

    for (di = dl; *di; di++) {
	FILE *f = fopen(*di, "r");
	char *ngs, *mod, *app, *forbidden, *good;
	int rc;

	if (!f) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE, "Cannot open %s: %m", *dl);
	    continue;
	}

	ngs = fgetheader(f, "Newsgroups:", 1);
	if (!ngs) {
	    ln_log(LNLOG_SERR, LNLOG_CARTICLE,
		   "Cannot read Newsgroups from %s, deleting", *dl);
	    log_unlink(*di, 0);
	    log_fclose(f);
	    continue;
	}

	if ((forbidden = checkstatus(ngs, 'n'))) {
	    ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		    "Article was posted to non-writable group %s", forbidden);
	    log_unlink(*di, 0);
	    log_fclose(f);
	    free(forbidden);
	    free(ngs);
	    continue;
	}

	good = checkstatus(ngs, 'y');
	mod = checkstatus(ngs, 'm');
	app = fgetheader(f, "Approved:", 1);

	if (good || (mod && app)) {
	    if ((rc = store(*di, 0, 0, 3))) {
		ln_log(LNLOG_SERR, LNLOG_CARTICLE, "Could not store %s: \"%s\", "
			"moving to %s/failed.postings/",
			*di, store_err(rc), spooldir);
		(void)log_moveto(*di, "/failed.postings/");
	    } else {
		log_unlink(*di, 0);
	    }
	} else {
	    if (!good && !mod) ln_log(LNLOG_SNOTICE, LNLOG_CARTICLE,
		    "Article was posted to unknown groups %s", ngs);
	    log_unlink(*di, 0);
	}
	log_fclose(f);
	if (good) free(good);
	if (mod) free(mod);
	if (app) free(app);
	free(ngs);
    }

    free_dirlist(dl);
    return 0;
}
