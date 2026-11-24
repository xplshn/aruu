/* see LICENSE file for copyright and license details */
#include "../diskutil.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

void
blockdev_sys_size(struct BlockDev *dev)
{
  uint64_t size_bytes = 0;
  int      sec_size   = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
  if (ioctl(dev->fd, BLKGETSIZE64, &size_bytes) >= 0)
    dev->size = size_bytes;
  if (ioctl(dev->fd, BLKSSZGET, &sec_size) >= 0)
    dev->sec_size = sec_size;
#pragma GCC diagnostic pop
}

void
blockdev_sys_reread(struct BlockDev *dev)
{
  struct stat st;

  // the kernel keeps the partition table it read when the disk appeared:
  // a table written here has no device nodes until it is asked to read
  // again, which fails harmlessly while a partition is in use
  if (fstat(dev->fd, &st) == 0 && S_ISBLK(st.st_mode))
    ioctl(dev->fd, BLKRRPART);
}

/* the model the disk reports, without the blanks firmware pads it with */
static void
disk_model(const char *name, char *out, size_t n)
{
  char  path[128];
  FILE *f;
  size_t len;

  out[0] = '\0';
  snprintf(path, sizeof(path), "/sys/block/%s/device/model", name);
  if (!(f = fopen(path, "r")))
    return;
  if (fgets(out, (int)n, f)) {
    out[strcspn(out, "\n")] = '\0';
    for (len = strlen(out); len > 0 && out[len - 1] == ' '; len--)
      out[len - 1] = '\0';
  }
  fclose(f);
}

struct BlockDevInfo *
blockdev_get_list(void)
{
  FILE                *fp;
  FILE                *mfile;
  char                 line[256];
  char                 mline[512];
  char                 devpath[128];
  char                 mnt[256];
  char                 fs[64];
  struct BlockDevInfo *head = NULL;
  struct BlockDevInfo *tail = NULL;
  struct BlockDevInfo *info;
  struct BlockDevInfo *curr;
  unsigned int         major, minor;
  unsigned long long   blocks;
  char                 name[64];
  int                  is_part;

  fp = fopen(ARUU_LINUX_PATH_PROC_PARTITIONS, "r");
  if (!fp)
    return NULL;

  while (fgets(line, sizeof(line), fp)) {
    if (sscanf(line, " %u %u %llu %63s", &major, &minor, &blocks, name) != 4)
      continue;

    info = ecalloc(1, sizeof(*info));

    strlcpy(info->name, name, sizeof(info->name));
    snprintf(info->path, sizeof(info->path), "%s/%s", ARUU_PATH_DEV, name);
    info->size  = blocks * 1024;
    info->major = major;
    info->minor = minor;

    /* search tree topology to link partition node under proper
 * target parent disk */
    is_part = 0;
    for (curr = head; curr; curr = curr->next) {
      size_t len = strlen(curr->name);
      if (strncmp(name, curr->name, len) == 0) {
        char c = name[len];
        if (curr->name[len - 1] >= '0' && curr->name[len - 1] <= '9') {
          if (c == 'p' && name[len + 1] >= '0' && name[len + 1] <= '9')
            is_part = 1;
        } else {
          if (c >= '0' && c <= '9')
            is_part = 1;
        }
        if (is_part) {
          strlcpy(info->type, "part", sizeof(info->type));
          struct BlockDevInfo *p = curr->parts;
          if (!p) {
            curr->parts = info;
          } else {
            while (p->next)
              p = p->next;
            p->next = info;
          }
          break;
        }
      }
    }

    if (!is_part) {
      strlcpy(info->type, "disk", sizeof(info->type));
      disk_model(info->name, info->model, sizeof(info->model));
      if (!head) {
        head = info;
      } else {
        tail->next = info;
      }
      tail = info;
    }
  }
  fclose(fp);

  mfile = fopen(ARUU_LINUX_PATH_PROC_MOUNTS, "r");
  if (mfile) {
    while (fgets(mline, sizeof(mline), mfile)) {
      if (sscanf(mline, "%127s %255s %63s", devpath, mnt, fs) == 3) {
        char  *devname = devpath;
        size_t dev_len = strlen(ARUU_PATH_DEV);
        if (strncmp(devpath, ARUU_PATH_DEV, dev_len) == 0 && devpath[dev_len] == '/')
          devname = devpath + dev_len + 1;
        struct BlockDevInfo *curr;
        for (curr = head; curr; curr = curr->next) {
          if (strcmp(curr->name, devname) == 0) {
            strlcpy(curr->mountpoint, mnt, sizeof(curr->mountpoint));
            strlcpy(curr->fstype, fs, sizeof(curr->fstype));
          }
          struct BlockDevInfo *part;
          for (part = curr->parts; part; part = part->next) {
            if (strcmp(part->name, devname) == 0) {
              strlcpy(part->mountpoint, mnt, sizeof(part->mountpoint));
              strlcpy(part->fstype, fs, sizeof(part->fstype));
            }
          }
        }
      }
    }
    fclose(mfile);
  }

  return head;
}
#endif
