

#include "config.h"
#include "util.h"

#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

static void
mode_to_str(mode_t mode, char *buf)
{
  buf[0]  = S_ISDIR(mode)    ? 'd'
            : S_ISLNK(mode)  ? 'l'
            : S_ISCHR(mode)  ? 'c'
            : S_ISBLK(mode)  ? 'b'
            : S_ISFIFO(mode) ? 'p'
            : S_ISSOCK(mode) ? 's'
                             : '-';
  buf[1]  = (mode & S_IRUSR) ? 'r' : '-';
  buf[2]  = (mode & S_IWUSR) ? 'w' : '-';
  buf[3]  = (mode & S_IXUSR) ? ((mode & S_ISUID) ? 's' : 'x') : ((mode & S_ISUID) ? 'S' : '-');
  buf[4]  = (mode & S_IRGRP) ? 'r' : '-';
  buf[5]  = (mode & S_IWGRP) ? 'w' : '-';
  buf[6]  = (mode & S_IXGRP) ? ((mode & S_ISGID) ? 's' : 'x') : ((mode & S_ISGID) ? 'S' : '-');
  buf[7]  = (mode & S_IROTH) ? 'r' : '-';
  buf[8]  = (mode & S_IWOTH) ? 'w' : '-';
  buf[9]  = (mode & S_IXOTH) ? ((mode & S_ISVTX) ? 't' : 'x') : ((mode & S_ISVTX) ? 'T' : '-');
  buf[10] = '\0';
}

#if FEATURE_STAT_FORMAT
static void
print_custom(const char *file, struct stat *st, const char *fmt)
{
  const char    *p;
  char           fmt_spec[64];
  int            fmt_len;
  char           buf[32];
  struct passwd *pw;
  struct group  *gr;

  for (p = fmt; *p;) {
    if (*p == '\\') {
      p++;
      switch (*p) {
        case 'n':
          putchar('\n');
          break;
          // ?man -t: sort or specify timestamp
        case 't':
          putchar('\t');
          break;
        case 'r':
          putchar('\r');
          break;
        case 'b':
          putchar('\b');
          break;
          // ?man -f: force the operation
        case 'f':
          putchar('\f');
          break;
        case '\\':
          putchar('\\');
          break;
        case '\0':
          return;
        default:
          putchar(*p);
          break;
      }
      p++;
      continue;
    }
    if (*p != '%') {
      putchar(*p);
      p++;
      continue;
    }
    p++;
    if (*p == '%') {
      putchar('%');
      p++;
      continue;
    }
    fmt_spec[0] = '%';
    fmt_len     = 1;
    while (*p == '-' || *p == '+' || *p == '0' || *p == ' ' || *p == '#') {
      if (fmt_len < 60)
        fmt_spec[fmt_len++] = *p;
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      if (fmt_len < 60)
        fmt_spec[fmt_len++] = *p;
      p++;
    }
    switch (*p) {
      case 'a':
        fmt_spec[fmt_len++] = 'o';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned int)(st->st_mode & 0777));
        break;
      case 'A':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        mode_to_str(st->st_mode, buf);
        printf(fmt_spec, buf);
        break;
      case 'b':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long long)st->st_blocks);
        break;
      case 'B':
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, 512U);
        break;
      case 'd':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long long)st->st_dev);
        break;
      case 'D':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'x';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long long)st->st_dev);
        break;
      case 'g':
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, st->st_gid);
        break;
      case 'G':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        gr                  = getgrgid(st->st_gid);
        printf(fmt_spec, gr ? gr->gr_name : "");
        break;
      case 'h':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long)st->st_nlink);
        break;
      case 'i':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long)st->st_ino);
        break;
      case 'n':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, file);
        break;
      case 'o':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (unsigned long)st->st_blksize);
        break;
      case 's':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->st_size);
        break;
        // ?man -t: sort or specify timestamp
      case 't':
        fmt_spec[fmt_len++] = 'x';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, major(st->st_rdev));
        break;
      case 'T':
        fmt_spec[fmt_len++] = 'x';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, minor(st->st_rdev));
        break;
      case 'u':
        fmt_spec[fmt_len++] = 'u';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, st->st_uid);
        break;
      case 'U':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        pw                  = getpwuid(st->st_uid);
        printf(fmt_spec, pw ? pw->pw_name : "");
        break;
      case 'X':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->st_atime);
        break;
      case 'Y':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->st_mtime);
        break;
      case 'Z':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->st_ctime);
        break;
      default:
        putchar('%');
        if (*p) {
          putchar(*p);
        } else {
          return;
        }
        break;
    }
    p++;
  }
  putchar('\n');
}
#endif

#if FEATURE_STAT_FILESYSTEM
static const char *
fs_type_name(long type)
{
  switch (type) {
    case 0x9fa0:
      return "proc";
    case 0x62656567:
      return "beegfs";
    case 0xef53:
      return "ext2/ext3";
    case 0x1cd1:
      return "devpctld";
    case 0xf15f:
      return "ecryptfs";
    case 0x65737566:
      return "fuse";
    case 0x4d44:
      return "msdos";
    case 0x564c:
      return "novell";
    case 0x6969:
      return "nfs";
    case 0x534f4654:
      return "overlay";
    case 0x73717368:
      return "squashfs";
    case 0x01021994:
      return "tmpfs";
    case 0x858458f6:
      return "ramfs";
    default:
      return "unknown";
  }
}

static void
show_statfs(const char *file, struct statfs *st)
{
  printf("  File: \"%s\"\n", file);
  printf(
      "    ID: %llx Namelen: %ld Type: %s\n",
      (unsigned long long)st->f_fsid.__val[0] | ((unsigned long long)st->f_fsid.__val[1] << 32),
      (long)st->f_namelen,
      fs_type_name(st->f_type)
  );
  printf(
      "Block size: %-10ld Fundamental block size: %ld\n",
      (long)st->f_bsize,
      (long)st->f_frsize ? (long)st->f_frsize : (long)st->f_bsize
  );
  printf(
      "Blocks: Total: %-10lld Free: %-10lld Available: %lld\n",
      (long long)st->f_blocks,
      (long long)st->f_bfree,
      (long long)st->f_bavail
  );
  printf("Inodes: Total: %-10lld Free: %lld\n", (long long)st->f_files, (long long)st->f_ffree);
}

#if FEATURE_STAT_FORMAT
static void
print_fs_custom(const char *file, struct statfs *st, const char *fmt)
{
  const char *p;
  char        fmt_spec[64];
  int         fmt_len;

  for (p = fmt; *p;) {
    if (*p == '\\') {
      p++;
      switch (*p) {
        case 'n':
          putchar('\n');
          break;
          // ?man -t: sort or specify timestamp
        case 't':
          putchar('\t');
          break;
        case 'r':
          putchar('\r');
          break;
        case 'b':
          putchar('\b');
          break;
          // ?man -f: force the operation
        case 'f':
          putchar('\f');
          break;
        case '\\':
          putchar('\\');
          break;
        case '\0':
          return;
        default:
          putchar(*p);
          break;
      }
      p++;
      continue;
    }
    if (*p != '%') {
      putchar(*p);
      p++;
      continue;
    }
    p++;
    if (*p == '%') {
      putchar('%');
      p++;
      continue;
    }
    fmt_spec[0] = '%';
    fmt_len     = 1;
    while (*p == '-' || *p == '+' || *p == '0' || *p == ' ' || *p == '#') {
      if (fmt_len < 60)
        fmt_spec[fmt_len++] = *p;
      p++;
    }
    while (*p >= '0' && *p <= '9') {
      if (fmt_len < 60)
        fmt_spec[fmt_len++] = *p;
      p++;
    }
    switch (*p) {
      case 'a':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->f_bavail);
        break;
      case 'b':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->f_blocks);
        break;
        // ?man -c: print count or perform stdout action
      case 'c':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->f_files);
        break;
      case 'd':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->f_ffree);
        break;
        // ?man -f: force the operation
      case 'f':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long long)st->f_bfree);
        break;
      case 'i':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'x';
        fmt_spec[fmt_len]   = '\0';
        printf(
            fmt_spec,
            (unsigned long long)st->f_fsid.__val[0]
                | ((unsigned long long)st->f_fsid.__val[1] << 32)
        );
        break;
      case 'l':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->f_namelen);
        break;
      case 'n':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, file);
        break;
      case 's':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->f_bsize);
        break;
      case 'S':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'd';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->f_frsize ? (long)st->f_frsize : (long)st->f_bsize);
        break;
        // ?man -t: sort or specify timestamp
      case 't':
        fmt_spec[fmt_len++] = 'l';
        fmt_spec[fmt_len++] = 'x';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, (long)st->f_type);
        break;
      case 'T':
        fmt_spec[fmt_len++] = 's';
        fmt_spec[fmt_len]   = '\0';
        printf(fmt_spec, fs_type_name(st->f_type));
        break;
      default:
        putchar('%');
        if (*p) {
          putchar(*p);
        } else {
          return;
        }
        break;
    }
    p++;
  }
  putchar('\n');
}
#endif
#endif

static void
show_stat_terse(const char *file, struct stat *st)
{
  printf("%s ", file);
  printf("%lu %lu ", (unsigned long)st->st_size, (unsigned long)st->st_blocks);
  printf("%04o %u %u ", st->st_mode & 0777, st->st_uid, st->st_gid);
  printf("%llx ", (unsigned long long)st->st_dev);
  printf("%lu %lu ", (unsigned long)st->st_ino, (unsigned long)st->st_nlink);
  printf("%d %d ", major(st->st_rdev), minor(st->st_rdev));
  printf("%ld %ld %ld ", st->st_atime, st->st_mtime, st->st_ctime);
  printf("%lu\n", (unsigned long)st->st_blksize);
}

static void
show_stat(const char *file, struct stat *st)
{
  char buf[100];

  printf("  File: ‘%s’\n", file);
  printf(
      "  Size: %lu\tBlocks: %lu\tIO Block: %lu\n",
      (unsigned long)st->st_size,
      (unsigned long)st->st_blocks,
      (unsigned long)st->st_blksize
  );
  printf(
      "Device: %xh/%ud\tInode: %lu\tLinks %lu\n",
      major(st->st_dev),
      minor(st->st_dev),
      (unsigned long)st->st_ino,
      (unsigned long)st->st_nlink
  );
  printf("Access: %04o\tUid: %u\tGid: %u\n", st->st_mode & 0777, st->st_uid, st->st_gid);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&st->st_atime));
  printf("Access: %s\n", buf);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&st->st_mtime));
  printf("Modify: %s\n", buf);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&st->st_ctime));
  printf("Change: %s\n", buf);
}

static void
usage(void)
{
  eprintf(
      "usage: %s [-L] [-t]"
#if FEATURE_STAT_FILESYSTEM
      " [-f]"
#endif
#if FEATURE_STAT_FORMAT
      " [-c format]"
#endif
      " [file...]\n",
      argv0
  );
}

// ?man stat: display file status
// ?man display file or filesystem status information
int
main(int argc, char *argv[])
{
  struct stat st;
#if FEATURE_STAT_FILESYSTEM
  struct statfs stfs;
  int           fflag = 0;
#endif
#if FEATURE_STAT_FORMAT
  char *format = NULL;
#endif
  int i, ret = 0;
  int (*fn)(const char *, struct stat *)        = lstat;
  char *fnname                                  = "lstat";
  void (*showstat)(const char *, struct stat *) = show_stat;

  ARGBEGIN
  {
    // ?man -L: specify option flag
    case 'L':
      fn     = stat;
      fnname = "stat";
      break;
    // ?man -t: sort or specify timestamp
    case 't':
      showstat = show_stat_terse;
      break;
#if FEATURE_STAT_FILESYSTEM
    // ?man -f: force the operation
    case 'f':
      fflag = 1;
      break;
#endif
#if FEATURE_STAT_FORMAT
    // ?man -c:str: print count or perform stdout action
    case 'c':
      format = EARGF(usage());
      break;
#endif
    default:
      usage();
  }
  ARGEND;

  if (argc == 0) {
#if FEATURE_STAT_FILESYSTEM
    if (fflag) {
      if (fstatfs(0, &stfs) < 0)
        eprintf("fstatfs <stdin>:");
#if FEATURE_STAT_FORMAT
      if (format)
        print_fs_custom("<stdin>", &stfs, format);
      else
#endif
        show_statfs("<stdin>", &stfs);
    } else {
#endif
      if (fstat(0, &st) < 0)
        eprintf("fstat <stdin>:");
#if FEATURE_STAT_FORMAT
      if (format)
        print_custom("<stdin>", &st, format);
      else
#endif
        show_stat("<stdin>", &st);
#if FEATURE_STAT_FILESYSTEM
    }
#endif
  }

  for (i = 0; i < argc; i++) {
#if FEATURE_STAT_FILESYSTEM
    if (fflag) {
      if (statfs(argv[i], &stfs) == -1) {
        weprintf("statfs %s:", argv[i]);
        ret = 1;
        continue;
      }
#if FEATURE_STAT_FORMAT
      if (format)
        print_fs_custom(argv[i], &stfs, format);
      else
#endif
        show_statfs(argv[i], &stfs);
    } else {
#endif
      if (fn(argv[i], &st) == -1) {
        weprintf("%s %s:", fnname, argv[i]);
        ret = 1;
        continue;
      }
#if FEATURE_STAT_FORMAT
      if (format)
        print_custom(argv[i], &st, format);
      else
#endif
        showstat(argv[i], &st);
#if FEATURE_STAT_FILESYSTEM
    }
#endif
  }

  return ret;
}