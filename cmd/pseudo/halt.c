/* see LICENSE file for copyright and license details */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500 /* sync */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "reboot.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-pr]\n", argv0);
}

// ?man halt: halt or reboot
// ?man halt, poweroff, or reboot the machine
int
main(int argc, char *argv[])
{
  int pflag = 0, rflag = 0;
  int how = REBOOT_HALT;

  ARGBEGIN
  {
    // ?man -p: preserve file attributes
    case 'p':
      pflag = 1;
      break;
    // ?man -r: operate recursively
    case 'r':
      rflag = 1;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc > 0)
    usage();

  sync();

  if (pflag && rflag)
    usage();

  if (pflag)
    how = REBOOT_POWEROFF;
  if (rflag)
    how = REBOOT_RESTART;

  if (machine_reboot(how) < 0)
    eprintf("reboot:");
  return 0;
}
