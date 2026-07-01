
#include <sys/syscall.h>

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-fw] module...\n", argv0);
}

// ?man rmmod: remove a module from the Linux kernel
// ?man arguments: module...
// ?man rmmod removes a kernel module from the running kernel
// ?man // ?man -f: force removal of a module even if it is busy or in use
// ?man // ?man -w: wait for the module to become unused before removing
int
main(int argc, char *argv[])
{
  char *mod, *p;
  int   i;
  int   flags = O_NONBLOCK;

  ARGBEGIN
  {
    // ?man -f: specify f option
    case 'f':
      flags |= O_TRUNC;
      break;
    // ?man -w: specify w option
    case 'w':
      flags &= ~O_NONBLOCK;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc < 1)
    usage();

  for (i = 0; i < argc; i++) {
    mod = argv[i];
    p   = strrchr(mod, '.');
    if (p && !strcmp(p, ".ko"))
      *p = '\0';
    if (syscall(__NR_delete_module, mod, flags) < 0)
      eprintf("delete_module:");
  }

  return 0;
}
