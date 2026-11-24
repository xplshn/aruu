/* see LICENSE file for copyright and license details */

#include <sys/types.h>

#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "net.h"
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
  char           name[NET_IFNAME_MAX] = "";
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

  fd = net_tun_open();
  if (fd < 0)
    eprintf("open /dev/net/tun:");

  if (argc > 0)
    estrlcpy(name, argv[0], sizeof(name));

  if (net_tun_attach(fd, name, sizeof(name), Tflag) < 0)
    eprintf("ioctl TUNSETIFF:");

  if (tflag) {
    if (net_tun_persist(fd, 1) < 0)
      eprintf("ioctl TUNSETPERSIST:");
    if (owner) {
      if (net_tun_owner(fd, owner_uid) < 0)
        eprintf("ioctl TUNSETOWNER:");
    }
    printf("Set '%s' persistent and owned by %u\n", name, owner_uid);
  } else if (dflag) {
    if (net_tun_persist(fd, 0) < 0)
      eprintf("ioctl TUNSETPERSIST:");
    printf("Set '%s' non-persistent\n", name);
  }

  close(fd);
  return 0;
}
