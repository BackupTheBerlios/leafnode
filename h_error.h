#ifndef H_ERROR_H
#define H_ERROR_H
extern int h_errno;
/*@observer@*/ const char *my_h_strerror(int he);
#endif
