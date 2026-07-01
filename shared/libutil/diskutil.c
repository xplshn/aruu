/* See LICENSE file for copyright and license details. */
#include "../diskutil.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/ioctl.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__Apple__)
#include <sys/disk.h>
#include <sys/ioctl.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/sysctl.h>
#elif defined(__FreeBSD__)
#include <dirent.h>
#include <sys/disk.h>
#include <sys/sysctl.h>
#endif
#endif

int
blockdev_open(struct BlockDev *dev, const char *path, int writable)
{
  struct stat st;
  int         fd, flags;

  flags = writable ? O_RDWR : O_RDONLY;
  fd    = open(path, flags);
  if (fd < 0)
    return -1;

  if (fstat(fd, &st) < 0) {
    close(fd);
    return -1;
  }

  dev->fd       = fd;
  dev->size     = st.st_size;
  dev->sec_size = 512;

  if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
#if defined(__linux__)
    uint64_t size_bytes = 0;
    int      sec_size   = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverflow"
    if (ioctl(fd, BLKGETSIZE64, &size_bytes) >= 0)
      dev->size = size_bytes;
    if (ioctl(fd, BLKSSZGET, &sec_size) >= 0)
      dev->sec_size = sec_size;
#pragma GCC diagnostic pop
#elif defined(__FreeBSD__)
    off_t        size_bytes = 0;
    unsigned int sec_size   = 0;
    if (ioctl(fd, DIOCGMEDIASIZE, &size_bytes) >= 0)
      dev->size = size_bytes;
    if (ioctl(fd, DIOCGSECTORSIZE, &sec_size) >= 0)
      dev->sec_size = sec_size;
#elif defined(__OpenBSD__) || defined(__NetBSD__)
    struct disklabel dl;
    if (ioctl(fd, DIOCGDINFO, &dl) >= 0) {
      dev->size     = (uint64_t)dl.d_secsize * DL_GETDSIZE(&dl);
      dev->sec_size = dl.d_secsize;
    }
#endif
  }

  return 0;
}

int
blockdev_read(struct BlockDev *dev, uint64_t sector, void *buf, size_t sector_count)
{
  off_t   offset = (off_t)sector * dev->sec_size;
  size_t  size   = sector_count * dev->sec_size;
  ssize_t n;

  if (lseek(dev->fd, offset, SEEK_SET) == (off_t)-1)
    return -1;

  n = read(dev->fd, buf, size);
  if (n < 0 || (size_t)n != size)
    return -1;

  return 0;
}

int
blockdev_write(struct BlockDev *dev, uint64_t sector, const void *buf, size_t sector_count)
{
  off_t   offset = (off_t)sector * dev->sec_size;
  size_t  size   = sector_count * dev->sec_size;
  ssize_t n;

  if (lseek(dev->fd, offset, SEEK_SET) == (off_t)-1)
    return -1;

  n = write(dev->fd, buf, size);
  if (n < 0 || (size_t)n != size)
    return -1;

  return 0;
}

void
blockdev_close(struct BlockDev *dev)
{
  if (dev->fd >= 0) {
    close(dev->fd);
    dev->fd = -1;
  }
}

struct BlockDevInfo *
blockdev_get_list(void)
{
#if defined(__linux__)
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

#elif defined(__FreeBSD__)
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

#elif defined(__OpenBSD__)
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

#else
  struct BlockDevInfo *head = NULL;
  struct BlockDevInfo *tail = NULL;
  struct BlockDevInfo *info;
  struct BlockDev      dev;
  char                 devname[16];
  char                 devpath[128];
  char                 c;

  for (c = 'a'; c <= 'z'; c++) {
    snprintf(devname, sizeof(devname), "sd%c", c);
    snprintf(devpath, sizeof(devpath), "%s/%s", ARUU_PATH_DEV, devname);
    if (blockdev_open(&dev, devpath, 0) == 0) {
      info = ecalloc(1, sizeof(*info));
      strlcpy(info->name, devname, sizeof(info->name));
      snprintf(info->path, sizeof(info->path), "%s/%s", ARUU_PATH_DEV, devname);
      strlcpy(info->type, "disk", sizeof(info->type));
      info->size = dev.size;
      if (!head) {
        head = info;
      } else {
        tail->next = info;
      }
      tail = info;
      blockdev_close(&dev);
    }
  }
  return head;
#endif
}

void
blockdev_free_list(struct BlockDevInfo *list)
{
  struct BlockDevInfo *curr = list;
  while (curr) {
    struct BlockDevInfo *next = curr->next;
    struct BlockDevInfo *part = curr->parts;
    while (part) {
      struct BlockDevInfo *next_part = part->next;
      free(part);
      part = next_part;
    }
    free(curr);
    curr = next;
  }
}

struct FsType {
  const char *name;
  size_t      magic_offset;
  size_t      magic_len;
  uint64_t    magic1;
  uint64_t    magic2;
  size_t      uuid_offset;
  size_t      uuid_len;
  size_t      label_offset;
  size_t      label_len;
};

static const struct FsType fstypes[] = {
    /* ext2/3/4 */
    {"ext", 1080, 2, 0xEF53, 0x53EF, 1128, 16, 1144, 16},
    /* vfat / fat32 */
    {"vfat", 82, 5, 0x3233544146ULL, 0, 67, 4, 71, 11},
    /* vfat / fat12/16 */
    {"vfat", 54, 4, 0x31544146, 0, 39, 4, 43, 11},
    /* ntfs */
    {"ntfs", 3, 4, 0x5346544e, 0, 72, 8, 0, 0},
    /* xfs */
    {"xfs", 0, 4, 0x42534658, 0x58465342, 32, 16, 108, 12},
    /* btrfs */
    {"btrfs", 65600, 8, 0x4D5F53665248425FULL, 0, 65803, 16, 65819, 256},
    /* f2fs */
    {"f2fs", 1024, 4, 0xF2F52010, 0x1020F5F2, 1132, 16, 1148, 512},
    /* ufs1 */
    {"ufs1", 9564, 4, 0x011954, 0x54190100, 8428, 8, 0, 0},
    /* ufs2 */
    {"ufs2", 66908, 4, 0x19540119, 0x19015419, 65772, 8, 66216, 32},
    /* fossil */
    {"fossil", 131200, 4, 0x2340A3B1, 0xB1A34023, 0, 0, 131072, 127},
};

static void
format_uuid(const unsigned char *buf, size_t len, const char *type, char *out, size_t out_len)
{
  size_t i;
  char  *p;

  p = out;
  if (strcmp(type, "vfat") == 0) {
    snprintf(out, out_len, "%02X%02X-%02X%02X", buf[3], buf[2], buf[1], buf[0]);
    return;
  }

  if (strcmp(type, "ntfs") == 0) {
    for (i = 8; i > 0; i--) {
      snprintf(p, out_len - (p - out), "%02X", buf[i - 1]);
      p += 2;
    }
    return;
  }

  for (i = 0; i < len; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      *p++ = '-';
    }
    snprintf(p, out_len - (p - out), "%02x", buf[i]);
    p += 2;
  }
  *p = '\0';
}

static int
zfs_detect(const unsigned char *buf, size_t len)
{
  size_t   offset;
  uint64_t magic;

  for (offset = 131072; offset + 8 <= len; offset += 1024) {
    magic = ((uint64_t)buf[offset + 7] << 56) | ((uint64_t)buf[offset + 6] << 48)
            | ((uint64_t)buf[offset + 5] << 40) | ((uint64_t)buf[offset + 4] << 32)
            | ((uint64_t)buf[offset + 3] << 24) | ((uint64_t)buf[offset + 2] << 16)
            | ((uint64_t)buf[offset + 1] << 8) | (uint64_t)buf[offset];

    if (magic == 0x00bab10c00000000ULL || magic == 0x0cb1ba0000000000ULL || magic == 0x00bab10cULL
        || magic == 0x0cb1ba00ULL)
      return 1;
  }
  return 0;
}

struct Plan9FsDef {
  const char *name;
  int         blocksize;
  int         namelen;
  int         dirblks;
  int         indirblks;
  int         daddrbits;
};

static const struct Plan9FsDef p9_fs_defs[] = {
    {"cwfs64x", 16384, 144, 6, 4, 64},
    {"cwfs64", 16384, 56, 6, 4, 64},
    {"cwfs", 16384, 28, 6, 2, 32},
    {"fs64", 8192, 56, 6, 4, 64},
    {"fs", 4096, 28, 6, 2, 32},
};

static int
plan9_magic_check(const unsigned char *p, const char **name)
{
  if (memcmp(p, "\x01\x1c\xe5\x0d", 4) == 0 || memcmp(p, "\x0d\xe5\x1c\x01", 4) == 0) {
    *name = "hjfs";
    return 1;
  }
  if (memcmp(p, "gefs", 4) == 0 || memcmp(p, "sfeg", 4) == 0) {
    *name = "gefs";
    return 1;
  }
  return 0;
}

static int
plan9_detect_other(const unsigned char *buf, size_t len, const char **name)
{
  if (len >= 4 && plan9_magic_check(buf, name))
    return 1;
  if (len >= 516 && plan9_magic_check(buf + 512, name))
    return 1;
  return 0;
}

static int
plan9_detect_text(const unsigned char *buf, size_t len, const char **name)
{
  int    blocksize = 0, namelen = 0, dirblks = 0, indirblks = 0, daddrbits = 0;
  char   line[128];
  char   key[64];
  int    val;
  size_t i = 0, j;

  if (len >= 256 + 15) {
    if (memcmp(buf + 256, "kfs wren device", 15) == 0) {
      *name = "kfs";
      return 1;
    }
  }

  if (len < 512)
    return 0;
  i = 512;
  if (len > 1536)
    len = 1536;

  while (i < len) {
    size_t k = 0;
    while (i < len && buf[i] != '\n' && k < sizeof(line) - 1) {
      line[k++] = buf[i++];
    }
    if (i < len && buf[i] == '\n')
      i++;
    line[k] = '\0';

    if (sscanf(line, "%63s %d", key, &val) == 2) {
      if (strcmp(key, "blocksize") == 0)
        blocksize = val;
      else if (strcmp(key, "namelen") == 0)
        namelen = val;
      else if (strcmp(key, "dirblks") == 0)
        dirblks = val;
      else if (strcmp(key, "indirblks") == 0)
        indirblks = val;
      else if (strcmp(key, "daddrbits") == 0)
        daddrbits = val;
    }
  }

  for (j = 0; j < LEN(p9_fs_defs); j++) {
    if (blocksize == p9_fs_defs[j].blocksize && namelen == p9_fs_defs[j].namelen
        && dirblks == p9_fs_defs[j].dirblks && indirblks == p9_fs_defs[j].indirblks
        && daddrbits == p9_fs_defs[j].daddrbits) {
      *name = p9_fs_defs[j].name;
      return 1;
    }
  }

  return 0;
}

int
blockdev_detect_fs(
    struct BlockDev *dev,
    char            *type,
    size_t           type_sz,
    char            *label,
    size_t           label_sz,
    char            *uuid,
    size_t           uuid_sz
)
{
  unsigned char *buf;
  size_t         read_size = 262144; /* 256kb */
  size_t         sectors_to_read;
  size_t         i, j;
  int            found = 0;

  if (dev->size < read_size)
    read_size = dev->size;

  buf             = emalloc(read_size);
  sectors_to_read = (read_size + dev->sec_size - 1) / dev->sec_size;
  if (blockdev_read(dev, 0, buf, sectors_to_read) < 0) {
    free(buf);
    return -2;
  }

  for (i = 0; i < LEN(fstypes); i++) {
    const struct FsType *fs = &fstypes[i];
    if (fs->magic_offset + fs->magic_len > read_size)
      continue;

    uint64_t val = 0;
    for (j = 0; j < fs->magic_len; j++) {
      val |= ((uint64_t)buf[fs->magic_offset + j]) << (8 * j);
    }

    if (val == fs->magic1 || (fs->magic2 && val == fs->magic2)) {
      const char *type_name = fs->name;
      if (strcmp(fs->name, "ext") == 0) {
        if (buf[1116] & 4)
          type_name = "ext3";
        else if (buf[1120] & 64)
          type_name = "ext4";
        else
          type_name = "ext2";
      }

      if (type && type_sz)
        strlcpy(type, type_name, type_sz);

      if (label && label_sz && fs->label_len && fs->label_offset + fs->label_len <= read_size) {
        snprintf(label, label_sz, "%.*s", (int)fs->label_len, buf + fs->label_offset);
        /* strip trailing spaces */
        char *end = label + strlen(label) - 1;
        while (end >= label && *end == ' ') {
          *end = '\0';
          end--;
        }
      }

      if (uuid && uuid_sz && fs->uuid_len && fs->uuid_offset + fs->uuid_len <= read_size) {
        format_uuid(buf + fs->uuid_offset, fs->uuid_len, fs->name, uuid, uuid_sz);
      }

      found = 1;
      break;
    }
  }

  if (!found && zfs_detect(buf, read_size)) {
    if (type && type_sz)
      strlcpy(type, "zfs_member", type_sz);
    found = 1;
  }

  if (!found) {
    const char *p9_name = NULL;
    if (plan9_detect_other(buf, read_size, &p9_name)
        || plan9_detect_text(buf, read_size, &p9_name)) {
      if (type && type_sz)
        strlcpy(type, p9_name, type_sz);
      found = 1;
    }
  }

  free(buf);
  return found ? 0 : -1;
}
