/* See LICENSE file for copyright and license details. */

#include <sys/syscall.h>

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
  int cmd = LINUX_REBOOT_CMD_HALT;

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
    cmd = LINUX_REBOOT_CMD_POWER_OFF;
  if (rflag)
    cmd = LINUX_REBOOT_CMD_RESTART;

  if (syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, NULL) < 0)
    eprintf("reboot:");
  return 0;
}
