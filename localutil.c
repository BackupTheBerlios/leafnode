/*
 * Implementation of local groups.
 *
 * Written by Joerg Dietrich <joerg@dietrich.net> and
 * Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
 * Copyright 2000.
 *
 * Based on ideas and code by Joerg Dietrich.
 *
 * See README for restrictions on the use of this software.
 */

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"

#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>

struct localgroup {
    char * name;
    struct localgroup * left;
    struct localgroup * right;
} ;

struct localgroup *local = NULL;

/*
 * for searching purposes, local group names are sorted into a tree
 *
 * The function is taken from insertgroup() of leafnode-1.9.7 and
 * somewhat modified.
 */
void insertlocal(const char *name) {
    struct localgroup ** a;
    char c;

    a = &local;
    while (a) {
	if (*a) {
	    c = strcasecmp((*a)->name, name);
	    if (c < 0)
		a = &((*a)->left);
	    else if (c > 0)
		a = &((*a)->right);
	    else {
		/* this shouldn't happen */
		ln_log(LNLOG_NOTICE, "Two identical local groups: %s = %s",
			(*a)->name, name);
		return;
	    }
	} else {
	    /* create new entry */
	    *a = (struct localgroup *)critmalloc(sizeof(struct localgroup),
		 "Building list of local groups");
	    if (!*a)
		return;
	    (*a)->left  = NULL;
	    (*a)->right = NULL;
	    (*a)->name  = strdup(name);
	    if ((*a)->name == NULL) {
		ln_log(LNLOG_ERR, "Not sufficient memory for newsgroup %s",
				 name);
		free(*a);
		return;
	    }
	    return;
	}
    }
}

/*
 * read local groups into an array
 *
 * This function reads a number of group names and descriptions from
 * a file into memory. It then checks whether these groups can already
 * be found in the active file; if not, they are added to the active
 * file.
 *
 * The code could be made more efficient by reading the whole file
 * in one go. However, this would make the function less simple and
 * has therefore at the moment not been attempted.
 *
 * The format of the file containing the local groups is
 * news.group.name[whitespace]Description
 */
void readlocalgroups(void) {
    char *l, *p;
    FILE *f;

    strcpy(s, spooldir);
    strcat(s, "/leaf.node/local.groups");

    if (!(f = fopen( s, "r"))) {
	/* not very dramatic because the user probably just
	   does not want local groups */
	ln_log(LNLOG_DEBUG, "unable to open %s: %s", s, strerror(errno));
	return;
    }

    debug = 0;

    while ((l = getaline( f)) != NULL ) {
	p = l;

	while (p && *p && !isspace((unsigned char)*p))
	    p++;
	while (p && *p && isspace((unsigned char)*p))
	    *p++ = '\0';

	/* l points to group name, p to description */
	insertgroup(l, 0, 0, time(NULL), p);
	insertlocal(l);
    }

    debug = debugmode;

    fclose(f);
    mergegroups();
}

/*
 * find whether a group is indeed a local one
 */
int islocalgroup(const char *groupname) {
    struct localgroup *a;
    int c;

    a = local;
    c = 1;
    while (a) {
	c = strcasecmp(a->name, groupname);
	if (c < 0) {
	    a = a->left;
	}
	else if (c > 0) {
	    a = a->right;
	} else {
	    return TRUE;
	}
    }
    return FALSE;
}

/*
 * find whether a comma-separated list of groups contains only local ones
 */
int islocal(const char * grouplist) {
    char * p, * q;
    int retval = TRUE;	/* assume that all groups are local */

    p = strdup(grouplist);
    do {
	while (isspace( (unsigned char)*p) )
	    p++;
	q = strchr(p, ',');
	if (q)
	    *q++ = '\0';
	if (!islocalgroup( p) ) 
	    retval = FALSE;
	p = q;
    } while (p && *p && retval);
    free(p);
    return retval;
}
