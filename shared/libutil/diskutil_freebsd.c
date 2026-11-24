/* see LICENSE file for copyright and license details */
#include "../diskutil.h"
#include "../util.h"

#if defined(__FreeBSD__) && !defined(ARUU_FORCE_POSIX)
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>

void
blockdev_sys_size(struct BlockDev *dev)
{
  off_t        size_bytes = 0;
  unsigned int sec_size   = 0;

  if (ioctl(dev->fd, DIOCGMEDIASIZE, &size_bytes) >= 0)
    dev->size = size_bytes;
  if (ioctl(dev->fd, DIOCGSECTORSIZE, &sec_size) >= 0)
    dev->sec_size = sec_size;
}

void
blockdev_sys_reread(struct BlockDev *dev)
{
  (void)dev;
}

struct BlockDevInfo *
blockdev_get_list(void)
{
  char                 disks[1024];
  char                 devpath[128];
  char                *tok;
  char                *ptr;
  struct BlockDevInfo *head = NULL;
  struct BlockDevInfo *tail = NULL;
  struct BlockDevInfo *info;
  struct BlockDevInfo *part;
  struct BlockDevInfo *p;
  struct BlockDev      dev;
  DIR                 *dir;
  struct dirent       *de;
  size_t               disk_len;
  size_t               len;
  struct statfs       *mntbuf;
  int                  mntsize;
  int                  i;

  len = sizeof(disks);
  if (sysctlbyname("kern.disks", disks, &len, NULL, 0) < 0)
    return NULL;

  ptr = disks;
  while ((tok = strsep(&ptr, " \t")) != NULL) {
    if (!*tok)
      continue;

    info = ecalloc(1, sizeof(*info));

    strlcpy(info->name, tok, sizeof(info->name));
    snprintf(info->path, sizeof(info->path), "%s/%s", ARUU_PATH_DEV, tok);
    strlcpy(info->type, "disk", sizeof(info->type));

    snprintf(devpath, sizeof(devpath), "%s/%s", ARUU_PATH_DEV, tok);
    if (blockdev_open(&dev, devpath, 0) == 0) {
      info->size = dev.size;
      blockdev_close(&dev);
    }

    dir = opendir(ARUU_PATH_DEV);
    if (dir) {
      disk_len = strlen(tok);
      while ((de = readdir(dir)) != NULL) {
        if (strncmp(de->d_name, tok, disk_len) == 0) {
          char c = de->d_name[disk_len];
          if (c == 's' || c == 'p') {
            part = ecalloc(1, sizeof(*part));
            strlcpy(part->name, de->d_name, sizeof(part->name));
            snprintf(part->path, sizeof(part->path), "%s/%s", ARUU_PATH_DEV, de->d_name);
            strlcpy(part->type, "part", sizeof(part->type));
            snprintf(devpath, sizeof(devpath), "%s/%s", ARUU_PATH_DEV, de->d_name);
            if (blockdev_open(&dev, devpath, 0) == 0) {
              part->size = dev.size;
              blockdev_close(&dev);
            }
            p = info->parts;
            if (!p) {
              info->parts = part;
            } else {
              while (p->next)
                p = p->next;
              p->next = part;
            }
          }
        }
      }
      closedir(dir);
    }

    if (!head) {
      head = info;
    } else {
      tail->next = info;
    }
    tail = info;
  }

  mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
  for (i = 0; i < mntsize; i++) {
    char  *frompath = mntbuf[i].f_mntfromname;
    char  *devname  = frompath;
    size_t dev_len  = strlen(ARUU_PATH_DEV);
    if (strncmp(frompath, ARUU_PATH_DEV, dev_len) == 0 && frompath[dev_len] == '/')
      devname = frompath + dev_len + 1;
    struct BlockDevInfo *curr;
    for (curr = head; curr; curr = curr->next) {
      if (strcmp(curr->name, devname) == 0) {
        strlcpy(curr->mountpoint, mntbuf[i].f_mntonname, sizeof(curr->mountpoint));
        strlcpy(curr->fstype, mntbuf[i].f_fstypename, sizeof(curr->fstype));
      }
      struct BlockDevInfo *part;
      for (part = curr->parts; part; part = part->next) {
        if (strcmp(part->name, devname) == 0) {
          strlcpy(part->mountpoint, mntbuf[i].f_mntonname, sizeof(part->mountpoint));
          strlcpy(part->fstype, mntbuf[i].f_fstypename, sizeof(part->fstype));
        }
      }
    }
  }

  return head;
}
#endif
