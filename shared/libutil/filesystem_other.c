#include "../filesystem.h"

#include <errno.h>
#include <string.h>

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
#endif
