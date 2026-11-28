/* see LICENSE file for copyright and license details */
#include "../diskutil.h"

#include <errno.h>

/* a kernel with no backend: a device's size is what fstat says, and the
 * list of disks is what opens under the names most systems give them */
#if !(defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__))  \
    || defined(ARUU_FORCE_POSIX)
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

int
blockdev_mount(const char *dev, const char *dir, const char *fstype, int flags, const char *data)
{
  (void)dev;
  (void)dir;
  (void)fstype;
  (void)flags;
  (void)data;
  errno = ENOSYS;
  return -1;
}

int
blockdev_unmount(const char *dir)
{
  (void)dir;
  errno = ENOSYS;
  return -1;
}

int
blockdev_poweroff(const char *disk)
{
  (void)disk;
  errno = ENOSYS;
  return -1;
}
#endif
