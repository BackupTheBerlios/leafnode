/*
libutil -- stuff dealing with articles

Written by Cornelius Krasel <krasel@wpxx02.toxi.uni-wuerzburg.de>.
Copyright 1998, 1999.

See README for restrictions on the use of this software.
*/

#include "leafnode.h"
#include "critmem.h"
#include "ln_log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

char * lookup_xref(char * xref, const char * group);

/*
 * return arbitrary header from a buffer
  */
char *mgetheader (const char *hdr, char *buf) {
    char *p, *q;
    char *value = NULL;
    int havehdr = FALSE;

    p = buf;
    while (!havehdr && p && *p) {
	if (strncasecmp( p, hdr, strlen(hdr)) == 0 ) {
	    havehdr = TRUE;
	    p += strlen(hdr)+1;
	    while (isspace((unsigned char)*p))
		p++;
	    q = strchr(p, '\n');
	    if (p && q) {
		value = critmalloc((size_t)(q-p+2), "Allocating space for header value");
		memset(value, 0, (size_t)(q-p+2));
		strncpy(value, p, (size_t)(q-p));
	     }
	} else {
	     q = strchr(p, '\n');
	     p = q+1;
	}
    }
    return value;
}

/*
 * find a header in an article and return it. Strip the name of the header
 */
char * fgetheader(FILE * f, const char * header) {
    char * hdr, *p ;
    size_t hlen;
    int next;

    if (!f)
	return NULL;
    rewind(f);
    debug = 0;
    next = 0;
    hdr = NULL;
    p = NULL;
    hlen = strlen(header);
    while (( p = getaline( f)) && *p ) {	/* read only headers */
	/* allow for multiline headers */
	if (isspace((unsigned char)*p) && hdr && next) {
	    hdr = critrealloc(hdr, strlen( hdr) + strlen( p ) + 2,
			     "Allocating space for multiline headers");
	    strcat(hdr , "\n");
	    strcat(hdr , p);
	} else if (( strncasecmp( p, header, hlen) == 0 ) ) {
	    p += hlen;
	    if (*p && *p == ':')
		p++;
	    while (p && *p && isspace((unsigned char)*p))
		p++;
	    hdr = strdup(p);
	    next = 1;
	} else
	    next = 0;
    }
    debug = debugmode;
    rewind(f);
    return hdr;
}

char * getheader(const char * filename, const char * header) {
    FILE * f;
    char * hdr;
    struct stat st ;

    if (stat( filename, &st) || !S_ISREG( st.st_mode ) )
	return NULL;
    if (( f = fopen( filename, "r") ) == 0 )
	return NULL;
    hdr = fgetheader(f, header);
    fclose(f);
    return hdr;
}

/*
 * store article in "filename" in /var/spool/news/message.id and the
 * corresponding newsgroups. No checks are made whether the filename
 * points to a real file or a directory.
 */
void storearticle (char * filename, char * msgid, char * newsgroups) {
    FILE * infile, * outfile;
    const char * outname ;
    char * l, * xref; 
    char * cxref = NULL;
    char * ssmid;
    char * p, * q;
    char * xrefno;
    static struct newsgroup * cg = NULL;
    char tmp[10];
    char tmpxref[4096] ; /* 1024 for newsgroups, plus article numbers */
    
    if (strcmp( filename, "stdin") != 0 ) {
	if (( infile = fopen( filename, "r") ) == NULL )
	    return;
    }
    else
	infile = stdin;
    outname = lookup(msgid);
    if (( outfile = fopen( outname, "r") ) ) {
	/* if an article is already present, we overwrite it to create a
	   corrected Xref: line. However, we have to parse the Xref:
	   line which is already in the article because otherwise
	   we'll create double instances of articles
	*/
	xref = fgetheader(outfile, "Xref:");
	fclose(outfile);
    } else
	xref = NULL;
    if (( outfile = fopen( outname, "w") ) == NULL )
	return;
    
    debug = 0;
    /* copy article headers except Xref: line */
    while (( l = getaline( infile) ) && strlen( l ) ) {
	if (strncmp( l, "Xref:", 5) ) 
	    fprintf(outfile, "%s\n", l);
    }
    
    debug = debugmode;
    /* create Xref: line and all the links in the newsgroups */
    p = newsgroups;
    strcpy(tmpxref, fqdn);
    while (p && *p) {
	cg = NULL;
	q = strchr(p, ',');
	if (q)
	    *q++ = '\0';
	if (*p && ( isinteresting( p) || create_all_links ) ) {
	    cg = findgroup(p);
	    if (cg && chdirgroup( p, TRUE) ) {
		if (xref)
		    cxref = strdup(xref);
		if ((xrefno = lookup_xref( cxref, cg->name)) != NULL ) {
		    strcpy(tmp, xrefno);
		    link(outname, tmp);
		} else {
		    do {
			sprintf(tmp, "%ld", ++cg->last);
			errno = 0;
		    } while (link( outname, tmp)<0 && errno==EEXIST );
		    if (errno)
			ln_log(LNLOG_ERR, "error linking %s into %s/%s: %s",
				outname, p, tmp, strerror(errno));
		    else if (verbose)
			fprintf(stderr, "storing %s as %s in %s\n",
				 outname, tmp, p);
		}
		if (cxref)
		    free(cxref);
		strcat(tmpxref, " ");
		strcat(tmpxref, p);
		strcat(tmpxref, ":");
		strcat(tmpxref, tmp);
	    }
	}	
	p = q;
    } /* while */
    fprintf(outfile, "Xref: %s\n\n", tmpxref);

    debug = 0;
    /* copy rest of article */
    while(( l = getaline( infile) ) &&
	   !(( *l)=='.' && ( strlen( l )==1 ) ) ) {
	fprintf(outfile, "%s\n", l);
    }
    debug = debugmode;
    fclose(infile);
    fclose(outfile);
    
    if (( ssmid = getheader( outname, "Supersedes") ) ) {
	supersede(ssmid);
	free(ssmid);
    }
}

/*
 * Returns the article number, if the article is already stored in group,
 * NULL otherwise.
 */
char * lookup_xref(char * xref, const char * group) {
    char * p, * q;
   
    if (!xref || !group)
	return NULL;
    p = strstr(xref, group);
    if (p)
	p += strlen(group);
    else
	return NULL;
    while(!isdigit( (unsigned char) *p) )
	    p++;
    q = strchr(p, ' ');
    if (q)
	*q++ = '\0';
    return p;
}

/*
 * delete a message
 */
void supersede(const char *msgid) {
    const char * filename;
    char *p, *q, *r, *hdr;
    struct stat st;
    
    filename = lookup(msgid);
    if (!filename)
        return;
    
    p = hdr = getheader(filename, "Xref:");
    if (!hdr)
        return;
    
    /* jump hostname entry */
    while (p && !isspace((unsigned char)*p))
        p++;
    while (p && isspace((unsigned char)*p))
        p++;
    /* now p points to the first newsgroup */
    
    /* unlink all the hardlinks in the various newsgroups directories */
    while (p && ( ( q = strchr( p, ':') ) != NULL ) ) {
        *q++ = '\0';
        if (chdirgroup( p, FALSE) ) {
            r = p;
            p = q;
	    q = strchr(q, ' ');
	    if (q)
		*q++ = '\0';
            if (unlink(p)) {
                ln_log(LNLOG_ERR, "Failed to unlink %s: %s: %s", r, p,
		       strerror(errno));
            } else {
		ln_log(LNLOG_INFO, "Superseded %s in %s", p, r);
            }
        }
	if (q)
	    p = q;
	else
	    p = NULL;
        while (p && isspace((unsigned char)*p))
            p++;
    }
    
    /* unlink the message-id hardlink */
    if (stat( filename, &st) )
        ln_log_sys(LNLOG_ERR, "cannot stat %s: %s", filename, 
		   strerror(errno));
    else if (st.st_nlink > 1)
        ln_log_sys(LNLOG_ERR, "%s: link count is %d", filename, st.st_nlink);
    else if (unlink( filename) )
        ln_log_sys(LNLOG_ERR, "Failed to unlink %s: %s", filename,
		    strerror(errno));
    else {
        ln_log(LNLOG_INFO, "Superseded %s", filename);
    }
    free(hdr);
}

/*
 * store articles in newsgroups which are already stored in
 * $SPOOLDIR/message.id/
 */
void store(const char * filename, FILE * filehandle, char * newsgroups,
	    const char * msgid)
{
    char tmp[10];
    static struct newsgroup * cg = NULL;
    char xrefincase[4096]; /* 1024 for newsgroups, plus article numbers */
    char * p;
    char * q;
    char * x;

    x = xrefincase;
    if (verbose > 2)
	printf("storing %s: %s\n", msgid, newsgroups);

    p = newsgroups;
    while (p && *p) {
	q = strchr(p, ',');
	if (q)
	    *q++ = '\0';
	if (*p) {
	    if (!cg || strcmp(cg->name, p)) {
		cg = findgroup(p);
		if (cg) {
		    if (isinteresting(cg->name) || create_all_links)
		        (void) chdirgroup(p, TRUE);
		    else
		    	cg = NULL;
		}
	    }
	    if (cg) {
		do {
		    sprintf(tmp, "%lu", ++cg->last);
		    errno = 0;
		    ln_log(LNLOG_DEBUG, "..as article %lu in %s", 
			       cg->last, cg->name);
		} while (link( filename, tmp)<0 && errno==EEXIST );
		if (errno)
		    ln_log(LNLOG_ERR, "error linking %s into %s: %s",
			    filename, p, strerror(errno));
		else {
		    sprintf(x, " %s:%lu", cg->name, cg->last);
		    x += strlen(x);
		}
	    } else {
		if (verbose > 2)
		    printf(".. discarding unknown group %s\n", p);
	    }
	}
	p = q;
    }
    fprintf(filehandle, "Xref: %s%s\n", fqdn, xrefincase);
}
