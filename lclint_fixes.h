#ifndef LCLINT_FIXES_H
#define LCLINT_FIXES_H

/* certain declarations splint/lclint does not know about
 * because they belong to unannotated libraries
 * or they are not standardized
 * or plainly missing in splint
 */

/* getaddrinfo stuff, stolen from RFC2133 */

struct addrinfo {
  int     ai_flags;     /* AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST */
  int     ai_family;    /* PF_xxx */
  int     ai_socktype;  /* SOCK_xxx */
  int     ai_protocol;  /* 0 or IPPROTO_xxx for IPv4 and IPv6 */
  size_t  ai_addrlen;   /* length of ai_addr */
  char   *ai_canonname; /* canonical name for nodename */
  struct sockaddr  *ai_addr; /* binary address */
  /*@null@*/
  struct addrinfo  *ai_next; /* next structure in linked list */
};

int
getaddrinfo(/*@null@*/ const char *nodename, /*@null@*/ const char *servname,
    const struct addrinfo *hints, /*@special@*/ struct addrinfo **res)
    /*@defines *res@*/ ;

void
freeaddrinfo(/*@only@*/ struct addrinfo *ai);

/*@dependent@*/ /*@observer@*/ char *
gai_strerror(int ecode);


/* PCRE stuff */

/*@constant int PCRE_MULTILINE;@*/
/*@constant int PCRE_ERROR_NOMATCH;@*/

typedef /*@abstract@*/ pcre;
typedef /*@abstract@*/ pcre_extra;

/*@only@*/ pcre *pcre_compile(const char *pattern, int options,
    /*@null@*/ /*@out@*/ const char **errptr, /*@notnull@*/ /*@out@*/ int *erroffset,
    /*@null@*/ const unsigned char *tableptr);

int pcre_exec(const pcre *code, const pcre_extra *extra,
    const char *subject, int length, int startoffset,
    int options, int *ovector, int ovecsize);

/*@null@*/ /*@out@*/ /*@only@*/ void *(*pcre_malloc)(size_t);

void (*pcre_free)(/*@out@*/ /*@only@*/ void *);

#endif /* not LCLINT_FIXES_H */
