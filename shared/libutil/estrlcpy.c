/* see LICENSE file for copyright and license details */
#include <sys/types.h>

#include "../util.h"

size_t
estrlcpy(char *dst, const char *src, size_t siz)
{
  size_t ret;

  if ((ret = strlcpy(dst, src, siz)) >= siz)
    eprintf("strlcpy: input string too long\n");

  return ret;
}
