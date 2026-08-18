/* see LICENSE file for copyright and license details */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-a]\n", argv0);
}

// ?man nproc: print the number of available processors
// ?man arguments: [-a]
// ?man with no options, prints the number of processors currently
// ?man online. -a prints the number installed, including any offline
int
main(int argc, char *argv[])
{
  int  aflag = 0;
  long n;

  ARGBEGIN
  {
    // ?man -a: count installed processors, not just online ones
    case 'a':
      aflag = 1;
      break;
    default:
      usage();
  }
  ARGEND;

  n = sysconf(aflag ? _SC_NPROCESSORS_CONF : _SC_NPROCESSORS_ONLN);
  if (n <= 0)
    n = 1;
  printf("%ld\n", n);
  return 0;
}
