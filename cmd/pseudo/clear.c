/* See LICENSE file for copyright and license details. */

#include <stdio.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s\n", argv0);
}

// ?man clear: clear terminal screen
// ?man clear the terminal screen and scrollback buffer
int
main(int argc, char *argv[])
{
  argv0 = argv[0], argc--, argv++;

  if (argc)
    usage();

  printf("\x1b[2J\x1b[H");

  return 0;
}
