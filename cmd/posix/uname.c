/* see LICENSE file for copyright and license details */

#include <sys/utsname.h>

#include <stdio.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-amnrsv]\n", argv0);
}

// ?man uname: print system info
// ?man display system hostname, kernel name, release, and architecture
int
main(int argc, char *argv[])
{
  struct utsname u;
  int            mflag = 0, nflag = 0, rflag = 0, sflag = 0, vflag = 0;

  ARGBEGIN
  {
    // ?man -a: print or show all entries
    case 'a':
      mflag = nflag = rflag = sflag = vflag = 1;
      break;
    // ?man -m: specify mode or limit
    case 'm':
      mflag = 1;
      break;
    // ?man -n: print line numbers or counts
    case 'n':
      nflag = 1;
      break;
    // ?man -r: operate recursively
    case 'r':
      rflag = 1;
      break;
    // ?man -s: silent mode or print summary
    case 's':
      sflag = 1;
      break;
    // ?man -v: verbose mode; show progress
    case 'v':
      vflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (argc)
    usage();

  if (uname(&u) < 0)
    eprintf("uname:");

  if (sflag || !(nflag || rflag || vflag || mflag))
    putword(stdout, u.sysname);
  if (nflag)
    putword(stdout, u.nodename);
  if (rflag)
    putword(stdout, u.release);
  if (vflag)
    putword(stdout, u.version);
  if (mflag)
    putword(stdout, u.machine);
  putchar('\n');

  return fshut(stdout, "<stdout>");
}
