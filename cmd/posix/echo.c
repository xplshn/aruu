/* see LICENSE file for copyright and license details */

#include "util.h"
#include <stdio.h>
#include <string.h>

// ?man echo: write arguments to stdout
// ?man print the specified arguments to standard output
int
main(int argc, char *argv[])
{
  int nflag = 0;

  (void)argc;
  argv0 = *argv, argv0 ? argv++ : (void *)0;

  if (*argv && !strcmp(*argv, "-n")) {
    nflag = 1;
    argv++;
  }

  for (; *argv; argc--, argv++)
    putword(stdout, *argv);
  if (!nflag)
    putchar('\n');

  return fshut(stdout, "<stdout>");
}
