/* See LICENSE file for copyright and license details. */

#include <stdio.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [string]\n", argv0);
}

// ?man yes: output string repeatedly
// ?man arguments: string
// ?man output a string repeatedly until terminated
int
main(int argc, char *argv[])
{
  const char *s;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  s = argc ? argv[0] : "y";
  for (;;)
    puts(s);
}
