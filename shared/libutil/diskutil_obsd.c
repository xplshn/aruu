/* see LICENSE file for copyright and license details */
#include "../diskutil.h"
#include "../util.h"

#if defined(__OpenBSD__) && !defined(ARUU_FORCE_POSIX)
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/sysctl.h>

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
  char                 disknames[1024];
  char                 devpath[128];
  char                *tok;
  char                *ptr;
  char                *colon;
  struct BlockDevInfo *head = NULL;
  struct BlockDevInfo *tail = NULL;
  struct BlockDevInfo *info;
  struct BlockDevInfo *part;
  struct BlockDevInfo *p;
  struct BlockDev      dev;
  size_t               len;
  int                  mib[2];
  char                 c;
  struct statfs       *mntbuf;
  int                  mntsize;
  int                  i;

  mib[0] = CTL_HW;
  mib[1] = HW_DISKNAMES;
  len    = sizeof(disknames);
  if (sysctl(mib, 2, disknames, &len, NULL, 0) < 0)
    return NULL;

  ptr = disknames;
  while ((tok = strsep(&ptr, ",")) != NULL) {
    colon = strchr(tok, ':');
    if (colon)
      *colon = '\0';
    if (!*tok)
      continue;

    info = ecalloc(1, sizeof(*info));

    strlcpy(info->name, tok, sizeof(info->name));
    snprintf(info->path, sizeof(info->path), "%s/r%sc", ARUU_PATH_DEV, tok);
    strlcpy(info->type, "disk", sizeof(info->type));

    snprintf(devpath, sizeof(devpath), "%s/r%sc", ARUU_PATH_DEV, tok);
    if (blockdev_open(&dev, devpath, 0) == 0) {
      info->size = dev.size;
      blockdev_close(&dev);
    }

    for (c = 'a'; c <= 'p'; c++) {
      if (c == 'c')
        continue;
      snprintf(devpath, sizeof(devpath), "%s/r%s%c", ARUU_PATH_DEV, tok, c);
      if (blockdev_open(&dev, devpath, 0) == 0) {
        part = ecalloc(1, sizeof(*part));
        snprintf(part->name, sizeof(part->name), "%s%c", tok, c);
        snprintf(part->path, sizeof(part->path), "%s/r%s%c", ARUU_PATH_DEV, tok, c);
        strlcpy(part->type, "part", sizeof(part->type));
        part->size = dev.size;
        p          = info->parts;
        if (!p) {
          info->parts = part;
        } else {
          while (p->next)
            p = p->next;
          p->next = part;
        }
        blockdev_close(&dev);
      }
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
