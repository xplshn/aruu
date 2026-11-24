/* see LICENSE file for copyright and license details */
#include "../reboot.h"

#include <errno.h>

#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
int
machine_reboot(int how)
{
  (void)how;
  errno = ENOTSUP;
  return -1;
}
#endif
