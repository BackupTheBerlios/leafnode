#ifndef MASTRING_H
#define MASTRING_H

#include <sys/types.h>		/* for size_t */
#include <stdio.h>

char *mastrcpy(char *dest, const char *src);
char *mastrncpy(char *dest, const char *src, size_t len);

struct mastr {
    char *dat;
    int bufsize;
    int len;
};

typedef struct mastr mastr;

#define MASTR_OOM_ABORT 1

mastr *mastr_new(long);
mastr *mastr_newstr(const char *);
int mastr_cpy(mastr *, const char *);
int mastr_cat(mastr *, const char *);
int mastr_vcat(mastr *, ...);
int mastr_resizekeep(mastr *, long);
int mastr_resizekill(mastr *, long);
ssize_t mastr_getln(mastr *, FILE *, ssize_t maxbytes);
#define mastr_autosize(m) do { (void)mastr_resizekeep(m, m->len); } while(0)
void mastr_delete(mastr *);
void mastr_clear(mastr *);
void mastr_triml(mastr * m);
void mastr_trimr(mastr * m);
void mastr_chop(mastr * m);
#define mastr_trim(m) do { mastr_triml(m); mastr_trimr(m); } while(0)
#define mastr_str(m) ((const char *)(m->dat))
#endif