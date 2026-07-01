/* See LICENSE file for copyright and license details. */

#include <sys/stat.h>

#include "fs.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-afipv] [-R [-H | -L | -P]] source ... dest\n", argv0);
}

// ?man cp: copy files and directories
// ?man arguments: source ... dest
// ?man copy files and directories to a destination
int
main(int argc, char *argv[])
{
  struct stat st;

  ARGBEGIN
  {
    // ?man -i: prompt before overwriting existing files
    case 'i':
      cp_iflag = 1;
      break;
    // ?man -a: archive mode; equivalent to -dpR
    case 'a':
      cp_follow = 'P';
      cp_aflag = cp_pflag = cp_rflag = 1;
      break;
    // ?man -f: force copy by removing existing destination files
    case 'f':
      cp_fflag = 1;
      break;
    // ?man -p: preserve file attributes
    case 'p':
      cp_pflag = 1;
      break;
    // ?man -r: copy directories recursively
    case 'r':
    // ?man -R: copy directories recursively
    case 'R':
      cp_rflag = 1;
      break;
    // ?man -v: verbose mode; show progress
    case 'v':
      cp_vflag = 1;
      break;
    // ?man -H: specify option flag
    case 'H':
    // ?man -L: specify option flag
    case 'L':
    // ?man -P: specify option flag
    case 'P':
      cp_follow = ARGC();
      break;
    default:
      usage();
  }
  ARGEND

  if (argc < 2)
    usage();

  if (!cp_follow)
    cp_follow = cp_rflag ? 'P' : 'L';

  if (argc > 2) {
    if (stat(argv[argc - 1], &st) < 0)
      eprintf("stat %s:", argv[argc - 1]);
    if (!S_ISDIR(st.st_mode))
      eprintf("%s: not a directory\n", argv[argc - 1]);
  }
  enmasse(argc, argv, cp);

  return fshut(stdout, "<stdout>") || cp_status;
}
