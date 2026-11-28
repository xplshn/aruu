/* see LICENSE file for copyright and license details */
#include "../kmap.h"
#include "../util.h"

/* a system kmap has no console backend for: the layouts are still listed
 * and written for xkb, only the console is not set */
#if !(defined(__linux__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
      defined(__FreeBSD__)) || defined(ARUU_FORCE_POSIX)
int
kmap_load_console(const struct kmap *k)
{
  (void)k;
  weprintf("this system's console takes no layout from kmap\n");
  return -1;
}
#endif
