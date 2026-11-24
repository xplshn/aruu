#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stddef.h>
#include <sys/types.h>

struct FilesystemStatus {
  unsigned long long block_size;
  unsigned long long total_blocks;
  unsigned long long free_blocks;
};

struct FilesystemMount {
  char *source;
  char *type;
  char *path;
};

int  fs_get_status(const char *, struct FilesystemStatus *);
int  fs_get_mounts(struct FilesystemMount **, size_t *);
void fs_free_mounts(struct FilesystemMount *, size_t);

/* each kernel packs the two numbers of a device its own way */
unsigned int fs_devmajor(dev_t);
unsigned int fs_devminor(dev_t);
dev_t        fs_makedev(unsigned int, unsigned int);

#endif
