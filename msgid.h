#ifndef MSGID_H
#define MSGID_H

void msgid_sanitize(char *m);
unsigned int msgid_hash(const char *name);
/*@dependent@*/ char *lookup(/*@null@*/ const char *msgid);
/*@falsewhennull@*/ int ihave(/*@null@*/ const char *mid);
int msgid_allocate(const char *file, const char *mid);
int msgid_deallocate(const char *file, const char *mid);

#endif
