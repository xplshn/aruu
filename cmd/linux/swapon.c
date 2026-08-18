/* see LICENSE file for copyright and license details */

#include <sys/swap.h>

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-dp] -a | device\n", argv0);
}

// ?man swapon: enable swap devices
// ?man arguments: -a | device
// ?man enable paging and swapping on specified devices
int
main(int argc, char *argv[])
{
  int            i;
  int            ret   = 0;
  int            flags = 0;
  int            all   = 0;
  struct mntent *me;
  FILE          *fp;

  ARGBEGIN
  {
    // ?man -a: print or show all entries
    case 'a':
      all = 1;
      break;
    // ?man -d: specify directory
    case 'd':
      flags |= SWAP_FLAG_DISCARD;
      break;
    // ?man -p: preserve file attributes
    case 'p':
      flags |= SWAP_FLAG_PREFER;
      break;
    default:
      usage();
  }
  ARGEND;

  if ((!all && argc < 1) || (all && argc > 0))
    usage();

  if (all) {
    fp = setmntent("/etc/fstab", "r");
    if (!fp)
      eprintf("setmntent %s:", "/etc/fstab");
    while ((me = getmntent(fp)) != NULL) {
      if (strcmp(me->mnt_type, MNTTYPE_SWAP) == 0 && (hasmntopt(me, MNTOPT_NOAUTO) == NULL)) {
        if (swapon(me->mnt_fsname, flags) < 0) {
          weprintf("swapon %s:", me->mnt_fsname);
          ret = 1;
        }
      }
    }
    endmntent(fp);
  } else {
    for (i = 0; i < argc; i++) {
      if (swapon(argv[i], flags) < 0) {
        weprintf("swapon %s:", argv[i]);
        ret = 1;
      }
    }
  }
  return ret;
}
