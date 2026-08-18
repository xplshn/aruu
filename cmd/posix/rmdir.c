/* see LICENSE file for copyright and license details */

#include <libgen.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-p] dir ...\n", argv0);
}

// ?man rmdir: remove empty directories
// ?man arguments: dir ...
// ?man remove empty directories from the filesystem
int
main(int argc, char *argv[])
{
  int   pflag = 0, ret = 0;
  char *d;

  ARGBEGIN
  {
    // ?man -p: remove parent directories if they are also empty
    case 'p':
      pflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();

  for (; *argv; argc--, argv++) {
    if (rmdir(*argv) < 0) {
      weprintf("rmdir %s:", *argv);
      ret = 1;
    } else if (pflag) {
      d = dirname(*argv);
      for (; strcmp(d, "/") && strcmp(d, ".");) {
        if (rmdir(d) < 0) {
          weprintf("rmdir %s:", d);
          ret = 1;
          break;
        }
        d = dirname(d);
      }
    }
  }

  return ret;
}
