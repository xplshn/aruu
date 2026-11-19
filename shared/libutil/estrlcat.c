/* see LICENSE file for copyright and license details */
#include <sys/types.h>

#include "../util.h"

size_t
estrlcat(char *dst, const char *src, size_t siz)
{
  size_t ret;

  if ((ret = strlcat(dst, src, siz)) >= siz)
    eprintf("strlcat: input string too long\n");

  return ret;
}
