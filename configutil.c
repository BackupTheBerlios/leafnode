/** \file configutil.c
 * read config file
 *
 * See AUTHORS for copyright holders and contributors.
 * See README for restrictions on the use of this software.
 */
#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"
#include "config_defs.h"
#include "configparam.h"
#include "get.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <sys/resource.h>

#ifdef WITH_DMALLOC
#include <dmalloc.h>
#endif

#define COREFILESIZE 1024*1024*4
#define TOKENSIZE 80
/*
 * misc. global variables, documented in leafnode.h
 */
char *mta = NULL;
time_t default_expire = 0;
/*@null@*/ struct expire_entry *expire_base = NULL;
/*@null@*/ struct delaybody_entry *delaybody_base = NULL;
unsigned long artlimit = 0;
unsigned long initiallimit = 0;
int create_all_links = 0;
int delaybody = 0;
int no_direct_spool = 0;
int timeout_long = 7;
int timeout_short = 2;
int timeout_active = 90;
int timeout_client = 300;
int authentication = 0;		/* use authentication for NNTP access:
				   possible values defined in leafnode.h */

int filtermode = FM_XOVER | FM_HEAD;
			/* filter xover headers or heads or both(default) */
long windowsize = 5;

/*@null@*/ char *filterfile = NULL;
/*@null@*/ char *pseudofile = NULL;	/* filename containing pseudoarticle body */
/*@null@*/ char *owndn = NULL;		/* own domain name, if you can't
					   set a sensible one */
/*@null@*/ struct serverlist *servers = NULL;
				/* global work variable */
static struct serverlist *serverlist;	/* local copy to free list */

/* parse a line, destructively */
int
parse_line(/*@unique@*/ char *l, /*@out@*/ char *param, /*@out@*/ char *value)
{
    char *p, *q;

    p = l;
    /* skip comments */
    q = strchr(p, '#');
    if (q)
	*q = '\0';
    if (*p == '\0')
	return 0;
    if ((q = strchr(p, '=')) == NULL)
	return 0;
    else
	*q++ = '\0';
    /* skipping leading blanks or tabs */
    SKIPLWS(p);
    SKIPLWS(q);
    strncpy(param, p, TOKENSIZE);
    strncpy(value, q, TOKENSIZE);
    /* now param contains the stuff before '=' and value the stuff behind it */
    if (!(*param) || !(*value))
	return 0;
    /* skipping trailing blanks or tabs */
    p = param + strlen(param) - 1;
    q = value + strlen(value) - 1;
    while (isspace((unsigned char)*p) && (p > param)) {
	*p = '\0';
	p--;
    }
    while (isspace((unsigned char)*q) && (q > value)) {
	*q = '\0';
	q--;
    }
    return 1;
}

/*
05/25/97 - T. Sweeney - Modified to read user name and password for AUTHINFO.
                        Security questionable as password is stored in
                        plaintext in insecure file.
*/
/*
 * read configuration. Return 0 upon success, values > 0(taken from
 * errno.h) otherwise
 */
int
readconfig(char *configfile)
{
    struct serverlist *p = NULL, *q = NULL;
    struct expire_entry *ent = NULL, *prev = NULL;
    FILE *f;
    char *l;
    char *param, *value;
    int err;
    time_t i;
    char s[PATH_MAX+1];

    artlimit = 0;
    param = (char *)critmalloc(TOKENSIZE, "allocating space for parsing");
    value = (char *)critmalloc(TOKENSIZE, "allocating space for parsing");
    if (configfile) {
	snprintf(s, PATH_MAX, "%s", configfile);
    } else {
	snprintf(s, PATH_MAX, "%s/config", libdir);
    }
    if ((f = fopen(s, "r")) == NULL) {
	err = errno;
	ln_log(LNLOG_SERR, LNLOG_CTOP, "cannot open %s: %m", s);
	free(param);
	free(value);
	return err;
    }
    while ((l = getaline(f))) {
	const struct configparam *cp;

	if (parse_line(l, param, value)) {
	    if ((cp = find_configparam(param))) {
		if (cp->scope == CS_SERVER && p == NULL) {
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   /* FIXME: use these as defaults instead */
			   "config: \"%s=%s\" requires server name, abort",
			   param, value);
		    return EINVAL;
		}
		if (cp->scope == CS_GLOBAL && p != NULL) {
		    ln_log(LNLOG_SWARNING, LNLOG_CTOP,
			   "config: \"%s=%s\" found in section of server %s, "
			   "please move it in front of any "
			   "server declaration", param, value, p->name);
		}
#ifdef __LCLINT__
		assert(p != NULL); /* can't happen */
#endif /* __LCLINT__ */
		switch (cp->code) {
		case CP_DEBUG:
		    debugmode |= strtol(value, NULL, 10);
		    ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
			       "config: debugmode is %d", debugmode);
		    break;
		case CP_MTA:
		    mta = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: mta is %s", mta);
		    break;
		case CP_USER:
		    if (p->username) free(p->username);
		    p->username = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: found username for %s", p->name);
		    break;
		case CP_PASS:
		    if (p->password) free(p->password);
		    p->password = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: found password for %s", p->name);
		    break;
		case CP_TIMEOUT:
		    p->timeout = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: timeout is %d seconds", p->timeout);
		    break;
		case CP_LINKS:
		    if (value && strlen(value)) {
			create_all_links = strtol(value, NULL, 10);
			if (create_all_links && debugmode & DEBUG_CONFIG)
			    ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				       "config: link articles in all groups");
		    }
		    break;
		case CP_EXPIRE:
		    {
			long days;

			if (!get_long(value, &days)) {
			    ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
				       "config: cannot parse %s=%s"
				       ": expected integer", param, value);
			}
			default_expire = time(0) -
			    (time_t) (SECONDS_PER_DAY * days);
			if (debugmode & DEBUG_CONFIG)
			    ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				       "config: expire is %s days", value);
		    }
		    break;
		case CP_FILTFIL:
		    filterfile = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: filterfile is %s", value);
		    break;
		case CP_HOST:
		case CP_FQDN:
		    owndn = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: hostname is %s", value);
		    break;
		case CP_AUTH:
		    authentication = AM_FILE;
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "authentication method: %d", authentication);
		    break;
		case CP_DELAY:
		    delaybody = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: delaybody is %d (default 0)",
				   delaybody);
		    break;
		case CP_AVOIDXOVER:
		    p->usexhdr = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: usexhdr is %d (default 0)",
				   p->usexhdr);
		    if (p->usexhdr && (filterfile || delaybody))
			ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
				   "config: usexhdr does not work together "
				   "with filtering or delaybody mode.");
		    break;
		case CP_TOSHORT:
		    timeout_short = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: timeout_short is %d days",
				   timeout_short);
		    break;
		case CP_TOLONG:
		    timeout_long = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: timeout_long is %d days",
				   timeout_long);
		    break;
		case CP_TOACTIVE:
		    timeout_active = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: timeout_active is %d days",
				   timeout_active);
		    break;
		case CP_TOCLIENT:
		    timeout_client = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: timeout_client is %d secs",
				   timeout_client);
		    break;
		case CP_WINDOW:
		    windowsize = strtol(value, NULL, 10);
		    if (windowsize < 1)
			windowsize = 1;
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: windowsize is %ld commands"
				   " (but limited by TCP send "
				   "buffer size)", windowsize);
		    break;
		case CP_GROUPEXP:
		    {
			char *m = value;

			while (*m && !(isspace((unsigned char)*m))) {
			    *m = tolower((unsigned char)*m);
			    m++;
			}
			*m++ = '\0';
			SKIPLWS(m);
			if (m && *m) {
			    long days;

			    if (!get_long(m, &days)) {
				ln_log_sys(LNLOG_SERR, LNLOG_CTOP,
					   "config: cannot parse "
					   "%s=%s line: expected integer",
					   param, value);
			    } else {
				i = time(NULL)
				    - (time_t) (SECONDS_PER_DAY * days);
				ent = (struct expire_entry *)
				    critmalloc(sizeof(struct expire_entry),
					       "parsing groupexpire");

				if (ent) {
				    ent->group =
					critstrdup(value, "readconfig");
				    ent->xtime = i;
				    ent->next = prev;
				    prev = ent;
				    if (debugmode & DEBUG_CONFIG)
					ln_log_sys(LNLOG_SDEBUG,
						   LNLOG_CTOP,
						   "config: groupexpire for %s is %ld days",
						   value, days);
				}
			    }
			}
		    }
		    break;
		case CP_GROUPDELAY:
		    {
			struct delaybody_entry *e = (struct delaybody_entry *)
			    critmalloc(sizeof *e, "parsing groupdelay");
			e->group = critstrdup(value, "parsing groupdelay");
			e->next = delaybody_base;
			delaybody_base = e;
			if (debugmode & DEBUG_CONFIG)
			    ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				    "config: groupdelay for group %s", value);
		    }
		    break;
		case CP_MAXAGE:
		case CP_MAXOLD:
		case CP_MAXXP:
		case CP_MAXGR:
		case CP_MAXLN:
		case CP_MINLN:
		case CP_MAXBYT:
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "%s is obsolete: use filterfile instead", param);
		    break;
		case CP_MAXFETCH:
		    artlimit = strtoul(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: maxfetch is %lu", artlimit);
		    break;
		case CP_PORT:
		    p->port = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: nntpport is %d", p->port);
		    break;
		case CP_NODESC:
		    p->descriptions = FALSE;
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: no LIST NEWSGROUPS for %s",
				   p->name);
		    break;
		case CP_INITIAL:
		    initiallimit = strtoul(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: initialfetch is %lu", initiallimit);
		    break;
		case CP_PSEUDO:
		    pseudofile = critstrdup(value, "readconfig");
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: read pseudoarticle from %s",
				   pseudofile);
		    break;
		case CP_DONTPOST:
		    p->dontpost = atoi(value);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: don't post is %d", p->dontpost);
		    break;
		case CP_SERVER:
		case CP_SUPPL:
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: server is %s", value);
		    p = (struct serverlist *)
			critmalloc(sizeof(struct serverlist),
				   "allocating space for server name");

		    p->name = critstrdup(value, "readconfig");
		    p->descriptions = TRUE;
		    p->next = NULL;
		    p->timeout = 30;	/* default 30 seconds */
		    p->port = 0;
		    p->usexhdr = 0;	/* default: use XOVER */
		    p->username = NULL;
		    p->password = NULL;
		    p->active = TRUE;
		    p->dontpost = FALSE;
		    if (!servers)
			servers = serverlist = p;
		    else
			q->next = p;
		    q = p;
		    break;
		case CP_NODIRECTSPOOL:
		    no_direct_spool = strtol(value, NULL, 10);
		    if (debugmode & DEBUG_CONFIG)
			ln_log_sys(LNLOG_SDEBUG, LNLOG_CTOP,
				   "config: no_direct_spool is %d (default 0)",
				   no_direct_spool);
		    break;
		default:
		    ln_log(LNLOG_SERR, LNLOG_CTOP,
			   "Config line \"%s=%s\" unhandled, abort.",
			   param, value);
		    abort();	/* DIE */
		    exit(127);	/* DIE! */
		    _exit(127);	/* DIE!! */
		    return EINVAL;	/* we don't get until here */
		}		/* switch */
	    } else {
		ln_log(LNLOG_SWARNING, LNLOG_CTOP,
		       "Unknown config line \"%s=%s\" ignored", param, value);
	    }
	}
    }
    expire_base = ent;
    (void)fclose(f);
    if (!default_expire)
	ln_log(LNLOG_SERR, LNLOG_CTOP, "no expire declaration in config file");
    if (!mta) {
	mta = critstrdup(DEFAULTMTA, "readconfig");
    }

    free(param);
    free(value);
    return 0;
}

static void
freeservers(void)
{
    struct serverlist *p = serverlist;

    while (p) {
	struct serverlist *t = p->next;
	if (p->name)
	    free(p->name);
	if (p->username)
	    free(p->username);
	if (p->password)
	    free(p->password);
	free(p);
	p = t;
    }

    serverlist = servers = 0;
}

static void
freeexpire(void)
{
    struct expire_entry *e = expire_base;

    while (e) {
	struct expire_entry *t = e->next;
	if (e->group)
	    free(e->group);
	free(e);
	e = t;
    }

    expire_base = 0;
}

static void
freedelaybody(void)
{
    struct delaybody_entry *t, *e = delaybody_base;
    while (e) {
	t = e->next;
	if (e->group)
	    free(e->group);
	free(e);
	e = t;
    }
    delaybody_base = NULL;
}

void
freeconfig(void)
{
    freeservers();
    if (owndn) {
	free(owndn);
	owndn = NULL;
    }
    if (pseudofile) {
	free(pseudofile);
	pseudofile = NULL;
    }
    if (filterfile) {
	free(filterfile);
	filterfile = NULL;
    }
    if (mta) {
        free(mta);
	mta = NULL;
    }
    freeexpire();
    freedelaybody();
}
