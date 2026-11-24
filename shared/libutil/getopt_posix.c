/* see LICENSE file for copyright and license details */
#include "../util.h"

#include <unistd.h>

/* the one reset posix names */
#if !(defined(__linux__) || defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) \
      || defined(__OpenBSD__) || defined(__APPLE__))                                             \
    || defined(ARUU_FORCE_POSIX)
void
getopt_reset(void)
{
  optind = 1;
}
#endif
