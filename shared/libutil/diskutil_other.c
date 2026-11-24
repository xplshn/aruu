/* see LICENSE file for copyright and license details */
#include "../diskutil.h"

/* a kernel with no backend: a device's size is what fstat says, and the
 * list of disks is what opens under the names most systems give them */
#if !(defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__))  \
    || defined(ARUU_FORCE_OTHER)
void
blockdev_sys_size(struct BlockDev *dev)
{
  (void)dev;
}

void
blockdev_sys_reread(struct BlockDev *dev)
{
  (void)dev;
}

struct BlockDevInfo *
blockdev_get_list(void)
{
  return blockdev_probe_list();
}
#endif
