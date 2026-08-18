/* see LICENSE file for copyright and license details */

#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-p] [-m mode] name ...\n", argv0);
}

// ?man mkdir: create directories
// ?man arguments: name ...
// ?man create directories at specified paths
int
main(int argc, char *argv[])
{
  mode_t mode, mask;
  int    pflag = 0, ret = 0;

  mask = umask(0);
  mode = 0777 & ~mask;

  ARGBEGIN
  {
    // ?man -p: create parent directories as needed
    case 'p':
      pflag = 1;
      break;
    // ?man -m:mode: set file mode bits for created directories
    case 'm':
      mode = parsemode(EARGF(usage()), 0777, mask);
      break;
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();

  for (; *argv; argc--, argv++) {
    if (pflag) {
      if (mkdirp(*argv, mode, 0777 & (~mask | 0300)) < 0)
        ret = 1;
    } else if (mkdir(*argv, mode) < 0) {
      weprintf("mkdir %s:", *argv);
      ret = 1;
    }
  }

  return ret;
}
