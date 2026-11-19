/* see LICENSE file for copyright and license details */
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>

#include "../util.h"

int
mkdirp(const char *path, mode_t mode, mode_t pmode)
{
  char        tmp[PATH_MAX], *p;
  struct stat st;

  if (stat(path, &st) == 0) {
    if (S_ISDIR(st.st_mode))
      return 0;
    errno = ENOTDIR;
    return -1;
  }

  if (strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  for (p = tmp + (tmp[0] == '/'); *p; p++) {
    if (*p != '/')
      continue;
    *p = '\0';
    if (mkdir(tmp, pmode) < 0 && errno != EEXIST)
      return -1;
    *p = '/';
  }
  if (mkdir(tmp, mode) < 0 && errno != EEXIST)
    return -1;
  return 0;
}
