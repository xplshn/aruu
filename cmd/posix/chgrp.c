/* see LICENSE file for copyright and license details */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <unistd.h>

#include "fs.h"
#include "util.h"

static int   hflag = 0;
static gid_t gid   = -1;
static int   ret   = 0;

static void
chgrp(int dirfd, const char *name, struct stat *st, void *data, struct recursor *r)
{
  int flags = 0;

  (void)data;

  if ((r->maxdepth == 0 && r->follow == 'P') || (r->follow == 'H' && r->depth)
      || (hflag && !(r->depth)))
    flags |= AT_SYMLINK_NOFOLLOW;
  if (fchownat(dirfd, name, -1, gid, flags) < 0) {
    weprintf("chown %s:", r->path);
    ret = 1;
  } else if (S_ISDIR(st->st_mode)) {
    recurse(dirfd, name, NULL, r);
  }
}

static void
usage(void)
{
  eprintf("usage: %s [-h] [-R [-H | -L | -P]] group file ...\n", argv0);
}

// ?man chgrp: change group ownership
// ?man arguments: group file ...
// ?man change the group ownership of files and directories
int
main(int argc, char *argv[])
{
  struct group   *gr;
  struct recursor r = {.fn = chgrp, .maxdepth = 1, .follow = 'P'};

  ARGBEGIN
  {
    // ?man -h: affect symbolic links instead of referenced files
    case 'h':
      hflag = 1;
      break;
    // ?man -R: change group ownership recursively
    case 'R':
      r.maxdepth = 0;
      break;
    // ?man -H: specify option flag
    case 'H':
    // ?man -L: specify option flag
    case 'L':
    // ?man -P: specify option flag
    case 'P':
      r.follow = ARGC();
      break;
    default:
      usage();
  }
  ARGEND

  if (argc < 2)
    usage();

  errno = 0;
  if ((gr = getgrnam(argv[0]))) {
    gid = gr->gr_gid;
  } else {
    if (errno)
      eprintf("getgrnam %s:", argv[0]);
    gid = estrtonum(argv[0], 0, UINT_MAX);
  }

  for (argc--, argv++; *argv; argc--, argv++)
    recurse(AT_FDCWD, *argv, NULL, &r);

  return ret || recurse_status;
}
