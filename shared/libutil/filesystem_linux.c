#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#if defined(__linux__)
#include "../filesystem.h"
#include "../paths.h"
#include "../util.h"

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

int
fs_get_status(const char *path, struct FilesystemStatus *status)
{
  struct statvfs s;

  memset(status, 0, sizeof(*status));
  if (statvfs(path, &s) < 0)
    return -1;
  status->block_size   = s.f_frsize;
  status->total_blocks = s.f_blocks;
  status->free_blocks  = s.f_bfree;
  return 0;
}

int
fs_get_mounts(struct FilesystemMount **mounts, size_t *count)
{
  struct FilesystemMount *list = NULL;
  struct mntent          *me;
  FILE                   *fp;
  size_t                  n = 0;

  *mounts = NULL;
  *count  = 0;
  fp      = setmntent(ARUU_LINUX_PATH_PROC_MOUNTS, "r");
  if (!fp)
    return -1;

  while ((me = getmntent(fp)) != NULL) {
    list           = erealloc(list, (n + 1) * sizeof(*list));
    list[n].source = estrdup(me->mnt_fsname);
    list[n].type   = estrdup(me->mnt_type);
    list[n].path   = estrdup(me->mnt_dir);
    n++;
  }
  endmntent(fp);

  *mounts = list;
  *count  = n;
  return 0;
}

void
fs_free_mounts(struct FilesystemMount *mounts, size_t count)
{
  size_t i;

  for (i = 0; i < count; i++) {
    free(mounts[i].source);
    free(mounts[i].type);
    free(mounts[i].path);
  }
  free(mounts);
}
#endif
