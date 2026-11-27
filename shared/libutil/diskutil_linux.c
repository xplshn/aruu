/* see LICENSE file for copyright and license details */
#define _DEFAULT_SOURCE /* realpath, sync */
#include "../diskutil.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define SYS_BLOCK ARUU_LINUX_PATH_SYS "/block"

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

/* a disk the kernel calls removable is a card reader or a disc drive;
 * a USB stick or an SD card often says it is not, so the bus it hangs
 * from counts as well */
static int
disk_removable(const char *name)
{
  char  path[PATH_MAX];
  char  real[PATH_MAX];
  FILE *f;
  int   c = 0;

  snprintf(path, sizeof(path), SYS_BLOCK "/%s/removable", name);
  if ((f = fopen(path, "r"))) {
    c = fgetc(f);
    fclose(f);
  }
  if (c == '1')
    return 1;
  snprintf(path, sizeof(path), SYS_BLOCK "/%s", name);
  if (!realpath(path, real))
    return 0;
  return strstr(real, "/usb") || strstr(real, "/mmc_host/");
}

/* the mounts file writes a blank, a tab, a newline and a backslash in a
 * path as octal escapes, which are undone in place */
static void
unescape_mount(char *s)
{
  char *w = s;

  for (; *s; s++) {
    if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0' && s[2] <= '7' && s[3] >= '0' && s[3] <= '7') {
      *w++ = (char)((s[1] - '0') << 6 | (s[2] - '0') << 3 | (s[3] - '0'));
      s += 3;
    } else {
      *w++ = *s;
    }
  }
  *w = '\0';
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
          info->removable = curr->removable;
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
      info->removable = disk_removable(info->name);
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
        unescape_mount(mnt);
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
int
blockdev_mount(const char *dev, const char *dir, const char *fstype, int flags, const char *data)
{
  unsigned long mf = 0;

  if (flags & BLOCKDEV_RDONLY)
    mf |= MS_RDONLY;
  if (flags & BLOCKDEV_NOSUID)
    mf |= MS_NOSUID;
  if (flags & BLOCKDEV_NODEV)
    mf |= MS_NODEV;
  if (flags & BLOCKDEV_NOEXEC)
    mf |= MS_NOEXEC;
  return mount(dev, dir, fstype, mf, data);
}

int
blockdev_unmount(const char *dir)
{
  return umount(dir);
}

static int
sys_write1(const char *path)
{
  int fd, n;

  if ((fd = open(path, O_WRONLY)) < 0)
    return -1;
  n = write(fd, "1", 1);
  close(fd);
  return n == 1 ? 0 : -1;
}

/* the USB device a disk hangs from: the nearest folder above it in sysfs
 * that has an idVendor, as a device has and its interfaces do not */
static int
usb_parent(const char *real, char *out, size_t n)
{
  char  path[PATH_MAX];
  char *slash;

  strlcpy(out, real, n);
  while ((slash = strrchr(out, '/')) && slash != out) {
    *slash = '\0';
    snprintf(path, sizeof(path), "%s/idVendor", out);
    if (access(path, F_OK) == 0)
      return 0;
  }
  return -1;
}

int
blockdev_poweroff(const char *disk)
{
  char path[PATH_MAX];
  char real[PATH_MAX];
  char usb[PATH_MAX];
  int  have_usb;

  if (strchr(disk, '/') || disk[0] == '.') {
    errno = EINVAL;
    return -1;
  }
  snprintf(path, sizeof(path), SYS_BLOCK "/%s", disk);
  if (!realpath(path, real))
    return -1;
  /* found before the disk is let go, which takes its sysfs folders away */
  have_usb = usb_parent(real, usb, sizeof(usb)) == 0;
  sync();
  /* the disk let go of: the kernel flushes its cache and forgets it */
  snprintf(path, sizeof(path), SYS_BLOCK "/%s/device/delete", disk);
  if (sys_write1(path) < 0 && !have_usb)
    return -1;
  if (!have_usb)
    return 0;
  /* the port cuts the unit's power, as a hub does for a device it removes */
  snprintf(path, sizeof(path), "%s/remove", usb);
  return sys_write1(path);
}
#endif
