/* see LICENSE file for copyright and license details */

#include <sys/stat.h>

#include <stdlib.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-m mode] name ...\n", argv0);
}

// ?man mkfifo: make fifos
// ?man arguments: name ...
// ?man create named pipes at specified paths
int
main(int argc, char *argv[])
{
  mode_t mode = 0666;
  int    ret  = 0;

  ARGBEGIN
  {
    // ?man -m:mode: specify mode or limit
    case 'm':
      mode = parsemode(EARGF(usage()), mode, umask(0));
      break;
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();

  for (; *argv; argc--, argv++) {
    if (mkfifo(*argv, mode) < 0) {
      weprintf("mkfifo %s:", *argv);
      ret = 1;
    }
  }

  return ret;
}
