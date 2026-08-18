/* see LICENSE file for copyright and license details */

#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s target name\n", argv0);
}

// ?man link: create a link to a file
// ?man arguments: target name
// ?man create a hard link to an existing file
int
main(int argc, char *argv[])
{
  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (argc != 2)
    usage();

  if (link(argv[0], argv[1]) < 0)
    eprintf("link:");

  return 0;
}
