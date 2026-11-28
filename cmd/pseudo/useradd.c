/* see LICENSE file for copyright and license details */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "accounts.h"
#include "fs.h"
#include "util.h"

#define SKEL "/etc/skel"

static uid_t owner;

static void
usage(void)
{
  eprintf("usage: %s [-m] [-R root] [-d home] [-s shell] [-p hash] [-G group,...] name\n", argv0);
}

static int
own(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
  (void)st;
  (void)type;
  (void)ftw;
  return lchown(path, owner, owner) < 0 ? -1 : 0;
}

/* the home: the skeleton copied, or an empty folder, all the person's */
static void
make_home(const char *home, uid_t uid)
{
  struct stat st;

  if (stat(SKEL, &st) == 0 && S_ISDIR(st.st_mode)) {
    cp_rflag = 1;
    cp_pflag = 1;
    if (cp(SKEL, home, 0) < 0 || cp_status)
      eprintf("%s: the skeleton could not be copied\n", home);
  } else if (mkdir(home, 0755) < 0 && errno != EEXIST) {
    eprintf("mkdir %s:", home);
  }
  owner = uid;
  if (nftw(home, own, 16, FTW_PHYS) < 0)
    eprintf("chown %s:", home);
}

// ?man useradd: add a person
// ?man arguments: [-m] [-R root] [-d home] [-s shell] [-p hash] [-G group,...] name
// ?man adds name to the account files with the next free id from 1000 and a group of its own of the same id
int
main(int argc, char *argv[])
{
  char *root = NULL, *home = NULL, *shell = "/bin/sh", *hash = "!", *groups = NULL;
  char  buf[PATH_MAX], *g, *save;
  int   make = 0;
  uid_t uid;

  ARGBEGIN
  {
    // ?man -m: make the home from /etc/skel
    case 'm': make = 1; break;
    // ?man -R:str: work in another root
    case 'R': root = EARGF(usage()); break;
    // ?man -d:str: the home, /home/name otherwise
    case 'd': home = EARGF(usage()); break;
    // ?man -s:str: the shell, /bin/sh otherwise
    case 's': shell = EARGF(usage()); break;
    // ?man -p:str: the password as crypt(3) gives it, locked otherwise
    case 'p': hash = EARGF(usage()); break;
    // ?man -G:str: the groups the person is put in, apart by commas
    case 'G': groups = EARGF(usage()); break;
    default: usage();
  }
  ARGEND;
  if (argc != 1)
    usage();
  if (root && (chroot(root) < 0 || chdir("/") < 0))
    eprintf("chroot %s:", root);
  if (!home) {
    snprintf(buf, sizeof(buf), "/home/%s", argv[0]);
    home = buf;
  }
  if (acct_add(argv[0], home, shell, hash, &uid) < 0)
    eprintf(errno == EEXIST ? "%s: there is one already\n" : errno == EINVAL ? "%s: not a name\n" : "%s:", argv[0]);
  for (g = groups ? strtok_r(groups, ",", &save) : NULL; g; g = strtok_r(NULL, ",", &save))
    if (acct_member(g, argv[0], 1) < 0)
      weprintf("%s:", g);
  if (make)
    make_home(home, uid);
  return 0;
}
