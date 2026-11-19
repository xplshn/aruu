/* see LICENSE file for copyright and license details */
#if FEATURE_MKNOD
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include "../fs.h"
#include "../util.h"

int cp_aflag  = 0;
int cp_fflag  = 0;
int cp_iflag  = 0;
int cp_pflag  = 0;
int cp_rflag  = 0;
int cp_vflag  = 0;
int cp_qflag  = 0;
int cp_status = 0;
int cp_errno  = 0;
int cp_follow;

/* a failure: counted, the first one kept in cp_errno, printed unless the
 * caller is a library that asked for quiet */
static int
cp_fail(const char *fmt, ...)
{
  va_list ap;
  int     err = errno;

  if (!cp_status)
    cp_errno = err;
  cp_status = 1;
  if (!cp_qflag) {
    va_start(ap, fmt);
    errno = err;
    xvprintf(fmt, ap);
    va_end(ap);
  }
  errno = err;
  return 0;
}

/* dir/name into buf, -1 when it does not fit */
static int
cp_join(char *buf, size_t n, const char *dir, const char *name)
{
  if (strlcpy(buf, dir, n) >= n || (dir[strlen(dir) - 1] != '/' && strlcat(buf, "/", n) >= n)
      || strlcat(buf, name, n) >= n) {
    errno = ENAMETOOLONG;
    return -1;
  }
  return 0;
}

int
cp(const char *s1, const char *s2, int depth)
{
  DIR            *dp;
  int             f1, f2, flags = 0;
  struct dirent  *d;
  struct stat     st, st2;
  struct timespec times[2];
  ssize_t         r;
  char            target[PATH_MAX], ns1[PATH_MAX], ns2[PATH_MAX];

  if (cp_follow == 'P' || (cp_follow == 'H' && depth))
    flags |= AT_SYMLINK_NOFOLLOW;

  if (fstatat(AT_FDCWD, s1, &st, flags) < 0)
    return cp_fail("stat %s:", s1);
  if (stat(s2, &st2) == 0 && st.st_dev == st2.st_dev && st.st_ino == st2.st_ino) {
    errno = EINVAL;
    return cp_fail("%s -> %s: same file\n", s1, s2);
  }

  if (cp_iflag && !cp_qflag && access(s2, F_OK) == 0) {
    if (!confirm("overwrite '%s'? ", s2))
      return 0;
  }

  if (cp_vflag && !cp_qflag)
    printf("%s -> %s\n", s1, s2);

  if (S_ISLNK(st.st_mode)) {
    if ((r = readlink(s1, target, sizeof(target) - 1)) >= 0) {
      target[r] = '\0';
      if (cp_fflag && unlink(s2) < 0 && errno != ENOENT)
        return cp_fail("unlink %s:", s2);
      else if (symlink(target, s2) < 0)
        return cp_fail("symlink %s -> %s:", s2, target);
    }
  } else if (S_ISDIR(st.st_mode)) {
    if (!cp_rflag) {
      errno = EISDIR;
      return cp_fail("%s is a directory\n", s1);
    }
    if (!(dp = opendir(s1)))
      return cp_fail("opendir %s:", s1);
    if (mkdir(s2, st.st_mode) < 0 && errno != EEXIST) {
      cp_fail("mkdir %s:", s2);
      closedir(dp);
      return 0;
    }

    while ((d = readdir(dp))) {
      if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, ".."))
        continue;
      if (cp_join(ns1, sizeof(ns1), s1, d->d_name) < 0
          || cp_join(ns2, sizeof(ns2), s2, d->d_name) < 0)
        cp_fail("%s/%s:", s1, d->d_name);
      else
        cp(ns1, ns2, depth + 1);
    }

    closedir(dp);
  } else if (
      cp_aflag
      && (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode) || S_ISSOCK(st.st_mode)
          || S_ISFIFO(st.st_mode))
  ) {
    if (cp_fflag && unlink(s2) < 0 && errno != ENOENT)
      return cp_fail("unlink %s:", s2);
    else if (mknod(s2, st.st_mode, st.st_rdev) < 0)
      return cp_fail("mknod %s:", s2);
  } else {
    if ((f1 = open(s1, O_RDONLY)) < 0)
      return cp_fail("open %s:", s1);
    if ((f2 = creat(s2, st.st_mode)) < 0 && cp_fflag) {
      if (unlink(s2) < 0 && errno != ENOENT) {
        cp_fail("unlink %s:", s2);
        close(f1);
        return 0;
      }
      f2 = creat(s2, st.st_mode);
    }
    if (f2 < 0) {
      cp_fail("creat %s:", s2);
      close(f1);
      return 0;
    }
    if ((r = copyall(f1, f2)) == -1)
      cp_fail("read %s:", s1);
    else if (r == -2)
      cp_fail("write %s:", s2);

    close(f1);
    close(f2);
    if (r < 0)
      return 0;
  }

  if (cp_aflag || cp_pflag) {
    /* atime and mtime */
    times[0] = st.st_atim;
    times[1] = st.st_mtim;
    if (utimensat(AT_FDCWD, s2, times, AT_SYMLINK_NOFOLLOW) < 0)
      cp_fail("utimensat %s:", s2);

    /* owner and mode */
    if (!S_ISLNK(st.st_mode)) {
      if (chown(s2, st.st_uid, st.st_gid) < 0) {
        cp_fail("chown %s:", s2);
        st.st_mode &= ~(S_ISUID | S_ISGID);
      }
      if (chmod(s2, st.st_mode) < 0)
        cp_fail("chmod %s:", s2);
    } else if (lchown(s2, st.st_uid, st.st_gid) < 0) {
      return cp_fail("lchown %s:", s2);
    }
  }

  return 0;
}
