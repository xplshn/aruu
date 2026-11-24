#include "../filesystem.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
int
fs_get_status(const char *path, struct FilesystemStatus *status)
{
  (void)path;
  memset(status, 0, sizeof(*status));
  errno = ENOTSUP;
  return -1;
}

int
fs_get_mounts(struct FilesystemMount **mounts, size_t *count)
{
  *mounts = NULL;
  *count  = 0;
  errno   = ENOTSUP;
  return -1;
}

void
fs_free_mounts(struct FilesystemMount *mounts, size_t count)
{
  (void)mounts;
  (void)count;
}

/* sys/types.h has them on most systems. without them, the split of
 * the first unixes: 8 bits each */
unsigned int
fs_devmajor(dev_t dev)
{
#if defined(major)
  return major(dev);
#else
  return ((unsigned int)dev >> 8) & 0xff;
#endif
}

unsigned int
fs_devminor(dev_t dev)
{
#if defined(minor)
  return minor(dev);
#else
  return (unsigned int)dev & 0xff;
#endif
}

dev_t
fs_makedev(unsigned int ma, unsigned int mi)
{
#if defined(makedev)
  return makedev(ma, mi);
#else
  return (dev_t)((ma & 0xff) << 8 | (mi & 0xff));
#endif
}
#endif
