/* see LICENSE file for copyright and license details */
#include "../../shared/libmagic/magic.h"
#include "arg.h"
#include "util.h"

#include <sys/stat.h>
#include <sys/types.h>
#ifndef major
#include <sys/sysmacros.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DESCLEN 4096
#define MIMELEN 256

enum Output { OUT_DESC, OUT_MIME_TYPE, OUT_MIME };

static struct Magic *db;
static char         *dbpath; /* NULL for the default databases */
static int           follow = 1;
static int           iflag;
static int           bflag;
static int           output = OUT_DESC;

static void
usage(void)
{
#if FEATURE_FILE_EXT
  eprintf("usage: %s [-bdhLi] [-M file] [-m file] [--mime-type | --mime] file ...\n", argv0);
#else
  eprintf(
      "usage: %s [-dh] [-M file] [-m file] file ...\n       %s -i [-h] file ...\n", argv0, argv0
  );
#endif
}

static void
dbpath_add(const char *path)
{
  size_t len = dbpath ? strlen(dbpath) : 0;

  dbpath = erealloc(dbpath, len + strlen(path) + 2);
  if (len)
    dbpath[len++] = ':';
  strcpy(dbpath + len, path);
}

static void
dbpath_add_default(void)
{
  const char *env;

  dbpath_add((env = getenv("MAGIC")) ? env : MAGIC_PATH_DEFAULT);
}

/* load the databases only once a regular file needs them */
static struct Magic *
dbget(void)
{
  if (!db && !(db = magic_open(dbpath)))
    eprintf("cannot load magic database %s:", dbpath ? dbpath : MAGIC_PATH_DEFAULT);
  return db;
}

static void
put(const char *name, const char *s)
{
  if (bflag)
    printf("%s\n", s);
  else
    printf("%s: %s\n", name, s);
}

static void
put_mime(const char *name, const char *type, const char *charset)
{
  char buf[MIMELEN + 64];

  if (output == OUT_MIME && charset)
    snprintf(buf, sizeof(buf), "%s; charset=%s", type, charset);
  else
    snprintf(buf, sizeof(buf), "%s", type);
  put(name, buf);
}

static void
cantopen(const char *name, int err)
{
  char buf[PATH_MAX + 256];

  snprintf(buf, sizeof(buf), "cannot open `%s' (%s)", name, strerror(err));
  put(name, buf);
}

/* the charset --mime prints, from the same bytes the engine judges it by */
static const char *
charset(int fd)
{
  static unsigned char buf[MAGIC_CHARSET_MAX];
  ssize_t              n;

  if ((n = pread(fd, buf, sizeof(buf), 0)) < 0)
    n = 0;
  return magic_charset(buf, (size_t)n);
}

static int
classify(const char *name, int fd, const char *prefix)
{
  char desc[DESCLEN], mime[MIMELEN], buf[DESCLEN + 64];

  if (magic_fd(dbget(), fd, desc, sizeof(desc), mime, sizeof(mime)) < 0) {
    cantopen(name, errno);
    return 1;
  }
  if (output != OUT_DESC) {
    put_mime(name, mime, charset(fd));
  } else {
    snprintf(buf, sizeof(buf), "%s%s%s", prefix, *prefix ? " " : "", desc);
    put(name, buf);
  }
  return 0;
}

/* types that come from the file system rather than from the bytes */
static void
put_fs(const char *name, const char *prefix, const char *desc, const char *mime, int charset)
{
  char buf[DESCLEN];

  if (output != OUT_DESC) {
    put_mime(name, mime, charset ? "binary" : NULL);
    return;
  }
  snprintf(buf, sizeof(buf), "%s%s%s", prefix, *prefix ? ", " : "", desc);
  put(name, buf);
}

static int
symlink_type(const char *name, const char *prefix)
{
  struct stat st;
  char        target[PATH_MAX], buf[PATH_MAX + 64];
  ssize_t     n;

  if ((n = readlink(name, target, sizeof(target) - 1)) <= 0) {
    snprintf(buf, sizeof(buf), "unreadable symlink `%s' (%s)", name, strerror(errno));
    put_fs(name, prefix, buf, "inode/symlink", 1);
    return 0;
  }
  target[n] = '\0';
  if (stat(name, &st) < 0) {
    snprintf(buf, sizeof(buf), "broken symbolic link to %s", target);
    put_fs(name, prefix, buf, "inode/symlink", 0);
    return 0;
  }
  snprintf(buf, sizeof(buf), "symbolic link to %s", target);
  put_fs(name, prefix, buf, "inode/symlink", 1);
  return 0;
}

static void
mode_prefix(char *buf, size_t n, mode_t mode)
{
  static const struct {
    mode_t      bit;
    const char *name;
  } bits[] = {{S_ISUID, "setuid"}, {S_ISGID, "setgid"}, {S_ISVTX, "sticky"}};
  size_t i, len = 0;

  buf[0] = '\0';
  for (i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
    if (mode & bits[i].bit) {
      snprintf(buf + len, n - len, "%s%s", len ? ", " : "", bits[i].name);
      len = strlen(buf);
    }
  }
}

static int
file(const char *name)
{
  struct stat st;
  char        prefix[64], buf[128];
  int         fd, ret;

  prefix[0] = '\0';
#if FEATURE_FILE_EXT
  if (strcmp(name, "-") == 0) {
    if (fstat(0, &st) < 0) {
      cantopen(name, errno);
      return 1;
    }
    return classify(name, 0, prefix);
  }
#endif
  /* a link to nothing is reported as a link even when following */
  if ((follow ? stat(name, &st) : lstat(name, &st)) < 0 && (!follow || lstat(name, &st) < 0)) {
    cantopen(name, errno);
    return 1;
  }
  mode_prefix(prefix, sizeof(prefix), st.st_mode);
  switch (st.st_mode & S_IFMT) {
    case S_IFDIR:
      put_fs(name, prefix, "directory", "inode/directory", 1);
      return 0;
    case S_IFCHR:
      snprintf(
          buf,
          sizeof(buf),
          "character special (%ld/%ld)",
          (long)major(st.st_rdev),
          (long)minor(st.st_rdev)
      );
      put_fs(name, prefix, buf, "inode/chardevice", 1);
      return 0;
    case S_IFBLK:
      snprintf(
          buf,
          sizeof(buf),
          "block special (%ld/%ld)",
          (long)major(st.st_rdev),
          (long)minor(st.st_rdev)
      );
      put_fs(name, prefix, buf, "inode/blockdevice", 1);
      return 0;
    case S_IFIFO:
      put_fs(name, prefix, "fifo (named pipe)", "inode/fifo", 1);
      return 0;
    case S_IFSOCK:
      put_fs(name, prefix, "socket", "inode/socket", 1);
      return 0;
    case S_IFLNK:
      return symlink_type(name, prefix);
    case S_IFREG:
      break;
    default:
      weprintf("%s: invalid mode 0%o\n", name, (unsigned)st.st_mode);
      return 1;
  }
  if (st.st_size == 0) {
    put_fs(name, prefix, "empty", "inode/x-empty", 1);
    return 0;
  }
  if (iflag) {
    put_fs(name, prefix, "regular file", "application/octet-stream", 1);
    return 0;
  }
  if ((fd = open(name, O_RDONLY)) < 0) {
    cantopen(name, errno);
    return 1;
  }
  ret = classify(name, fd, prefix);
  close(fd);
  return ret;
}

// ?man file: determine file type
// ?man arguments: file ...
// ?man file classifies each file by the file system first (directory, device, fifo,
// ?man socket, symbolic link, empty) and then by its bytes, with the magic(5) tests of
// ?man the file(1) databases: a compiled magic.mgc or the text sources. without -M
// ?man the default databases are MAGIC from the environment, else a built in list
int
main(int argc, char *argv[])
{
  int ret = 0, Mflag = 0, dflag = 0;

  ARGBEGIN
  {
    // ?man -d: apply the default tests, where it appears among -M and -m
    case 'd':
      if (!dflag)
        dbpath_add_default();
      dflag = 1;
      break;
    // ?man -h: identify a symbolic link itself, not the file it points to
    case 'h':
      follow = 0;
      break;
    // ?man -i: dont classify regular files further than regular file
    case 'i':
      iflag = 1;
      break;
    // ?man -M:file: use the tests in file instead of the default tests
    case 'M':
      dbpath_add(EARGF(usage()));
      Mflag = 1;
      break;
    // ?man -m:file: use the tests in file before the default tests
    case 'm':
      dbpath_add(EARGF(usage()));
      break;
#if FEATURE_FILE_EXT
    // ?man -b: brief, dont prefix lines with the file name
    case 'b':
      bflag = 1;
      break;
    // ?man -L: follow symbolic links
    case 'L':
      follow = 1;
      break;
    // ?man --mime-type: print the mime type instead of the description
    // ?man --mime: print the mime type and the charset
    case '-':
      if (strcmp(argv[0], "-mime-type") == 0)
        output = OUT_MIME_TYPE;
      else if (strcmp(argv[0], "-mime") == 0)
        output = OUT_MIME;
      else
        usage();
      brk_ = 1;
      break;
#endif
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();
  if (dbpath && !Mflag && !dflag)
    dbpath_add_default();
  for (; *argv; argv++)
    ret |= file(*argv);
  magic_close(db);
  free(dbpath);

  if (fshut(stdout, "<stdout>"))
    ret = 1;
  return ret;
}
