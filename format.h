/* format.h

   (C) 2000 - 2001 by Matthias Andree

*/

#ifndef FORMAT_H
#define FORMAT_H
/* format u as decimal number into str */
void str_ulong(char *str, unsigned long u);
/* 
 * format last len digits of u as decimal number into str, filling
 * with zeroes 
 */
void str_ulong0(char *str, unsigned long u, unsigned int len);
#endif
