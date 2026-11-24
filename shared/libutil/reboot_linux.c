/* see LICENSE file for copyright and license details */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../reboot.h"

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

int
machine_reboot(int how)
{
  int cmd;

  switch (how) {
    case REBOOT_POWEROFF:
      cmd = LINUX_REBOOT_CMD_POWER_OFF;
      break;
    case REBOOT_RESTART:
      cmd = LINUX_REBOOT_CMD_RESTART;
      break;
    default:
      cmd = LINUX_REBOOT_CMD_HALT;
      break;
  }
  return syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, NULL) < 0 ? -1 : 0;
}
#endif
