/* see LICENSE file for copyright and license details */
#include "../diskutil.h"

#if defined(__NetBSD__) && !defined(ARUU_FORCE_POSIX)
#include <errno.h>
#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>

void
blockdev_sys_size(struct BlockDev *dev)
{
  struct disklabel dl;

  if (ioctl(dev->fd, DIOCGDINFO, &dl) >= 0) {
    dev->size     = (uint64_t)dl.d_secsize * DL_GETDSIZE(&dl);
    dev->sec_size = dl.d_secsize;
  }
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
