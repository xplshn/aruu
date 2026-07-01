/* See LICENSE file for copyright and license details. */

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include "paths.h"
#include "util.h"

static int   dflag;
static int   tflag = 1;
static int   Tflag;
static char *owner;

static void
usage(void)
{
  eprintf("usage: %s [-dtT] [-u owner] [device]\n", argv0);
}

// ?man tunctl: configure tun/tap interfaces
// ?man arguments: device
// ?man create or destroy tun/tap network interfaces
int
main(int argc, char *argv[])
{
  struct ifreq   ifr;
  struct passwd *pw;
  uid_t          owner_uid = 0;
  int            fd;

  ARGBEGIN
  {
    // ?man -d: specify directory
    case 'd':
      dflag = 1;
      tflag = 0;
      break;
    // ?man -t: sort or specify timestamp
    case 't':
      tflag = 1;
      dflag = 0;
      break;
    // ?man -T: specify option flag
    case 'T':
      Tflag = 1;
      break;
    // ?man -u:str: unbuffered output
    case 'u':
      owner = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND

  if (owner) {
    pw = getpwnam(owner);
    if (!pw)
      owner_uid = estrtonum(owner, 0, UINT_MAX);
    else
      owner_uid = pw->pw_uid;
  }

  fd = open("/dev/net/tun", O_RDWR);
  if (fd < 0)
    eprintf("open /dev/net/tun:");

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = (Tflag ? IFF_TAP : IFF_TUN) | IFF_NO_PI;

  if (argc > 0)
    estrlcpy(ifr.ifr_name, argv[0], sizeof(ifr.ifr_name));

  if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0)
    eprintf("ioctl TUNSETIFF:");

  if (tflag) {
    if (ioctl(fd, TUNSETPERSIST, (void *)1) < 0)
      eprintf("ioctl TUNSETPERSIST:");
    if (owner) {
      if (ioctl(fd, TUNSETOWNER, (void *)(long)owner_uid) < 0)
        eprintf("ioctl TUNSETOWNER:");
    }
    printf("Set '%s' persistent and owned by %u\n", ifr.ifr_name, owner_uid);
  } else if (dflag) {
    if (ioctl(fd, TUNSETPERSIST, (void *)0) < 0)
      eprintf("ioctl TUNSETPERSIST:");
    printf("Set '%s' non-persistent\n", ifr.ifr_name);
  }

  close(fd);
  return 0;
}
