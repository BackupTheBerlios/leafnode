#ifndef MSGID_H
#define MSGID_H

void msgid_sanitize(char *m);
unsigned int msgid_hash(const char *name);
char *lookup (const char *msgid);
int ihave (const char *mid);
int msgid_allocate(const char *file, const char *mid);

#endif
