/* see LICENSE file for copyright and license details */
#include <stdio.h>
#include <unistd.h>

#include "../util.h"

/* -1 when reading failed and -2 when writing did, errno says why */
int
copyall(int f1, int f2)
{
  char    buf[BUFSIZ];
  ssize_t n;

  while ((n = read(f1, buf, sizeof(buf))) > 0) {
    if (writeall(f2, buf, n) < 0)
      return -2;
  }
  return n < 0 ? -1 : 0;
}
