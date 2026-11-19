/* see LICENSE file for copyright and license details */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../fs.h"
#include "../util.h"

int recurse_status = 0;

/* a failure of the walk at r->path: SILENT ones are ignored, QUIET ones
 * only counted */
static void
recurse_fail(struct recursor *r, const char *what)
{
  if (r->flags & SILENT)
    return;
  if (!r->err)
    r->err = errno;
  if (!(r->flags & QUIET))
    weprintf("%s %s:", what, r->path);
  recurse_status = 1;
}

void
recurse(int dirfd, const char *name, void *data, struct recursor *r)
{
  struct dirent *d;
  struct history *new, *h;
  struct stat st, dst;
  DIR        *dp;
  int         flags   = 0, fd;
  size_t      pathlen = r->pathlen;

  if (dirfd == AT_FDCWD && (pathlen = strlcpy(r->path, name, sizeof(r->path))) >= sizeof(r->path)) {
    errno = ENAMETOOLONG;
    recurse_fail(r, "path");
    return;
  }

  if (r->follow == 'P' || (r->follow == 'H' && r->depth))
    flags |= AT_SYMLINK_NOFOLLOW;

  if (fstatat(dirfd, name, &st, flags) < 0) {
    recurse_fail(r, "stat");
    return;
  }
  if (!S_ISDIR(st.st_mode)) {
    r->fn(dirfd, name, &st, data, r);
    return;
  }

  if (!(new = malloc(sizeof(struct history)))) {
    recurse_fail(r, "malloc");
    return;
  }
  new->prev = r->hist;
  r->hist   = new;
  new->dev  = st.st_dev;
  new->ino  = st.st_ino;

  for (h = new->prev; h; h = h->prev)
    if (h->ino == st.st_ino && h->dev == st.st_dev)
      return;

  if (!r->depth && (r->flags & DIRFIRST))
    r->fn(dirfd, name, &st, data, r);

  if (!r->maxdepth || r->depth + 1 < r->maxdepth) {
    if ((fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC | O_DIRECTORY)) < 0) {
      recurse_fail(r, "open");
      return;
    }
    if (!(dp = fdopendir(fd))) {
      recurse_fail(r, "fdopendir");
      close(fd);
      return;
    }
    if (r->path[pathlen - 1] != '/')
      r->path[pathlen++] = '/';
    if (r->follow == 'H')
      flags |= AT_SYMLINK_NOFOLLOW;
    while ((d = readdir(dp))) {
      if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
        continue;
      r->pathlen = pathlen + strlcpy(r->path + pathlen, d->d_name, sizeof(r->path) - pathlen);
      if (r->pathlen >= sizeof(r->path)) {
        errno = ENAMETOOLONG;
        recurse_fail(r, "path");
      } else if (fstatat(fd, d->d_name, &dst, flags) < 0) {
        recurse_fail(r, "stat");
      } else if ((r->flags & SAMEDEV) && dst.st_dev != st.st_dev) {
        continue;
      } else {
        r->depth++;
        r->fn(fd, d->d_name, &dst, data, r);
        r->depth--;
      }
    }
    r->path[pathlen - 1] = '\0';
    r->pathlen           = pathlen - 1;
    closedir(dp);
  }

  if (!r->depth) {
    if (!(r->flags & DIRFIRST))
      r->fn(dirfd, name, &st, data, r);

    while (r->hist) {
      h       = r->hist;
      r->hist = r->hist->prev;
      free(h);
    }
  }
}
