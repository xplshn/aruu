/* See LICENSE file for copyright and license details. */
#ifndef ARUU_DISKUTIL_H
#define ARUU_DISKUTIL_H

#include "paths.h"

#include <stddef.h>
#include <stdint.h>

struct BlockDev {
  int      fd;
  uint64_t size;
  size_t   sec_size;
};

struct BlockDevInfo {
  char                 name[32];
  char                 path[128];
  char                 type[16];
  uint64_t             size;
  int                  major;
  int                  minor;
  char                 mountpoint[256];
  char                 fstype[32];
  struct BlockDevInfo *parts;
  struct BlockDevInfo *next;
};

int  blockdev_open(struct BlockDev *dev, const char *path, int writable);
int  blockdev_read(struct BlockDev *dev, uint64_t sector, void *buf, size_t sector_count);
int  blockdev_write(struct BlockDev *dev, uint64_t sector, const void *buf, size_t sector_count);
void blockdev_close(struct BlockDev *dev);

int blockdev_detect_fs(
    struct BlockDev *dev,
    char            *type,
    size_t           type_sz,
    char            *label,
    size_t           label_sz,
    char            *uuid,
    size_t           uuid_sz
);

struct BlockDevInfo *blockdev_get_list(void);
void                 blockdev_free_list(struct BlockDevInfo *list);

#endif
