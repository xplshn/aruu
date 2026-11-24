#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../filesystem.h"
#include "../paths.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

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

unsigned int
fs_devmajor(dev_t dev)
{
  return major(dev);
}

unsigned int
fs_devminor(dev_t dev)
{
  return minor(dev);
}

dev_t
fs_makedev(unsigned int ma, unsigned int mi)
{
  return makedev(ma, mi);
}

int
fs_get_info(const char *path, int fd, struct FilesystemInfo *info)
{
  struct statfs st;

  if ((path ? statfs(path, &st) : fstatfs(fd, &st)) < 0)
    return -1;
  info->id = (unsigned long long)st.f_fsid.__val[0]
             | ((unsigned long long)st.f_fsid.__val[1] << 32);
  info->namelen = st.f_namelen;
  info->type    = st.f_type;
  info->bsize   = st.f_bsize;
  info->frsize  = st.f_frsize;
  info->blocks  = st.f_blocks;
  info->bfree   = st.f_bfree;
  info->bavail  = st.f_bavail;
  info->files   = st.f_files;
  info->ffree   = st.f_ffree;
  return 0;
}

const char *
fs_type_name(long type)
{
  switch (type) {
    case 0x9fa0:
      return "proc";
    case 0x62656567:
      return "beegfs";
    case 0xef53:
      return "ext2/ext3";
    case 0x1cd1:
      return "devpctld";
    case 0xf15f:
      return "ecryptfs";
    case 0x65737566:
      return "fuse";
    case 0x4d44:
      return "msdos";
    case 0x564c:
      return "novell";
    case 0x6969:
      return "nfs";
    case 0x534f4654:
      return "overlay";
    case 0x73717368:
      return "squashfs";
    case 0x01021994:
      return "tmpfs";
    case 0x858458f6:
      return "ramfs";
    default:
      return "unknown";
  }
}
#endif
