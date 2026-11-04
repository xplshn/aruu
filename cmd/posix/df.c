/* see LICENSE file for copyright and license details */

#include "filesystem.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static long blksize = 512;
static int  aflag   = 0;
static int  hflag   = 0;
static int  kflag   = 0;

#define CALC_POWER(n, power, base, i)                                                              \
  do {                                                                                             \
    while (n > power) {                                                                            \
      power = power * base;                                                                        \
      i++;                                                                                         \
    }                                                                                              \
  } while (0)

static void
print_human(
    const char        *fsname,
    unsigned long long total,
    unsigned long long used,
    unsigned long long avail,
    int                capacity,
    const char        *dir
)
{
  long               base        = 1024;
  unsigned long long power_total = base;
  unsigned long long power_used  = base;
  unsigned long long power_avail = base;
  char               postfixes[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E'};
  int                i = 0, j = 0, k = 0;

  total = total * blksize;
  used  = used * blksize;
  avail = avail * blksize;

  CALC_POWER(total, power_total, base, i);
  CALC_POWER(used, power_used, base, j);
  CALC_POWER(avail, power_avail, base, k);

  total = i ? total / (power_total / base) : total;
  used  = j ? used / (power_used / base) : used;
  avail = k ? avail / (power_avail / base) : avail;
  printf(
      "%-12s %9llu%c %9llu%c %9llu%c %7d%%  %s\n",
      fsname,
      total,
      postfixes[i],
      used,
      postfixes[j],
      avail,
      postfixes[k],
      capacity,
      dir
  );
}

static int
mnt_show(const char *fsname, const char *dir)
{
  struct FilesystemStatus status;
  unsigned long long      total, used, avail;
  int                     capacity = 0;
  int                     bs;

  if (fs_get_status(dir, &status) < 0)
    return -1;

  bs    = status.block_size / blksize;
  total = status.total_blocks * bs;
  avail = status.free_blocks * bs;
  used  = total - avail;

  if (used + avail) {
    capacity = (used * 100) / (used + avail);
    if (used * 100 != capacity * (used + avail))
      capacity++;
  }

  if (hflag)
    print_human(fsname, total, used, avail, capacity, dir);
  else
    printf("%-12s %9llu %9llu %9llu %7d%%  %s\n", fsname, total, used, avail, capacity, dir);

  return 0;
}

static void
usage(void)
{
  eprintf("usage: %s [-a]\n", argv0);
}

// ?man df: report disk space usage
// ?man display free and used disk space on filesystems
int
main(int argc, char *argv[])
{
  struct FilesystemMount *mounts = NULL;
  size_t                  count;
  size_t                  i;
  int                     ret = 0;

  ARGBEGIN
  {
    // ?man -a: print or show all entries
    case 'a':
      aflag = 1;
      break;
    // ?man -h: suppress headers or print help
    case 'h':
      hflag = 1;
      kflag = 0;
      break;
    // ?man -k: specify option flag
    case 'k':
      kflag   = 1;
      hflag   = 0;
      blksize = 1024;
      break;
    // ?man -s: silent mode or print summary
    case 's':
      eprintf("-s is not supported\n");
      break;
    // ?man -i: interactive mode or prompt for confirmation
    case 'i':
      eprintf("-i is not supported\n");
      break;
    default:
      usage();
  }
  ARGEND;

  if (hflag)
    printf(
        "Filesystem         Size       Used      "
        "Avail Capacity   Mounted on\n"
    );
  else
    printf(
        "Filesystem  %ld-blocks      Used     "
        "Avail Capacity  Mounted on\n",
        blksize
    );

  if (fs_get_mounts(&mounts, &count) < 0) {
    if (errno == ENOTSUP)
      eprintf("filesystem mount table is not supported\n");
    eprintf("cannot read filesystem mount table:");
  }
  for (i = 0; i < count; i++) {
    if (aflag == 0 && strcmp(mounts[i].type, "rootfs") == 0)
      continue;
    if (mnt_show(mounts[i].source, mounts[i].path) < 0)
      ret = 1;
  }
  fs_free_mounts(mounts, count);

  return ret;
}
