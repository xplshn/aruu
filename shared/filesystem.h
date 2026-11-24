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

/* a filesystem as stat -f shows it. type is the kernel's number for the
 * kind of filesystem, 0 where it has none */
struct FilesystemInfo {
  unsigned long long id;
  long               namelen;
  long               type;
  long               bsize;
  long               frsize;
  unsigned long long blocks;
  unsigned long long bfree;
  unsigned long long bavail;
  unsigned long long files;
  unsigned long long ffree;
};

int  fs_get_status(const char *, struct FilesystemStatus *);
int  fs_get_mounts(struct FilesystemMount **, size_t *);
void fs_free_mounts(struct FilesystemMount *, size_t);
/* the filesystem path is on, or fd's when path is null */
int         fs_get_info(const char *, int, struct FilesystemInfo *);
const char *fs_type_name(long);

/* each kernel packs the two numbers of a device its own way */
unsigned int fs_devmajor(dev_t);
unsigned int fs_devminor(dev_t);
dev_t        fs_makedev(unsigned int, unsigned int);

#endif
