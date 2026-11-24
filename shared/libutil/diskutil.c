/* see LICENSE file for copyright and license details */
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
  dev->dirty    = 0;

  if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode))
    blockdev_sys_size(dev);

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
  dev->dirty = 1;

  return 0;
}

void
blockdev_close(struct BlockDev *dev)
{
  if (dev->dirty && dev->fd >= 0)
    blockdev_sys_reread(dev);
  if (dev->fd >= 0) {
    close(dev->fd);
    dev->fd = -1;
  }
}

struct BlockDevInfo *
blockdev_probe_list(void)
{
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
    /* iso9660: "CD001" at the primary volume descriptor, LBA 16 */
    {"iso9660", 32769, 5, 0x3130304443ULL, 0, 0, 0, 32808, 32},
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
