/** amiroot.c - A program that exits 0 (true) if euid == 0.
 *
 * (C) 2001 by Matthias Andree
 * redistributable under the Lesser GNU Public License v2.1
 */

#include <unistd.h>
#include <sys/types.h>

int main(void) { return geteuid() ? 1 : 0; }
