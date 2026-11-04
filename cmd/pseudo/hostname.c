/* see LICENSE file for copyright and license details */

#if FEATURE_HOSTNAME_SET
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [name]\n", argv0);
}

// ?man hostname: show or set hostname
// ?man arguments: name
// ?man display or configure the system hostname
int
main(int argc, char *argv[])
{
  char host[HOST_NAME_MAX + 1];

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (!argc) {
    if (gethostname(host, sizeof(host)) < 0)
      eprintf("gethostname:");
    puts(host);
  } else if (argc == 1) {
#if FEATURE_HOSTNAME_SET
    if (sethostname(argv[0], strlen(argv[0])) < 0)
      eprintf("sethostname:");
#else
    eprintf("hostname: setting not supported\n");
#endif
  } else {
    usage();
  }

  return fshut(stdout, "<stdout>");
}
