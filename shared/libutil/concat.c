/* see LICENSE file for copyright and license details */
#include <unistd.h>

#include "../util.h"

int
concat(int f1, const char *s1, int f2, const char *s2)
{
  switch (copyall(f1, f2)) {
    case -1:
      weprintf("read %s:", s1);
      return -1;
    case -2:
      weprintf("write %s:", s2);
      return -2;
  }
  return 0;
}
