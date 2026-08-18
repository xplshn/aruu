/* see LICENSE file for copyright and license details */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
#if FEATURE_READLINK_REALPATH
  eprintf("usage: %s [-fn] path\n", argv0);
#else
  eprintf("usage: %s [-n] path\n", argv0);
#endif
}

// ?man readlink: print value of symlink
// ?man arguments: path
// ?man display the target of a symbolic link
int
main(int argc, char *argv[])
{
  char    buf[PATH_MAX];
  ssize_t n;
  int     nflag = 0;
#if FEATURE_READLINK_REALPATH
  int fflag = 0;
#endif

  ARGBEGIN
  {
#if FEATURE_READLINK_REALPATH
    // ?man -f: force the operation
    case 'f':
      fflag = 1;
      break;
#endif
    // ?man -n: print line numbers or counts
    case 'n':
      nflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (argc != 1)
    usage();

  if (strlen(argv[0]) >= PATH_MAX)
    eprintf("path too long\n");

#if FEATURE_READLINK_REALPATH
  if (fflag) {
    if (!realpath(argv[0], buf))
      eprintf("realpath %s:", argv[0]);
  } else
#endif
  {
    if ((n = readlink(argv[0], buf, PATH_MAX - 1)) < 0)
      eprintf("readlink %s:", argv[0]);
    buf[n] = '\0';
  }

  fputs(buf, stdout);
  if (!nflag)
    putchar('\n');

  return fshut(stdout, "<stdout>");
}
