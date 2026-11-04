#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stddef.h>

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

#endif
