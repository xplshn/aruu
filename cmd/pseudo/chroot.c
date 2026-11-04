/* see LICENSE file for copyright and license details */

#if FEATURE_CHROOT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#endif

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"
#include "wexec.h"

static void
usage(void)
{
  eprintf("usage: %s dir [cmd [arg ...]]\n", argv0);
}

// ?man chroot: run command in new root
// ?man arguments: dir [cmd [arg ...]]
// ?man run a command or shell with a substitute root directory
int
main(int argc, char *argv[])
{
  char *shell[] = {"/bin/sh", "-i", NULL}, *aux, *cmd;
  int   savederrno;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();

  if ((aux = getenv("SHELL")))
    shell[0] = aux;

#if FEATURE_CHROOT
  if (chroot(argv[0]) < 0)
    eprintf("chroot %s:", argv[0]);
#else
  eprintf("chroot: not supported\n");
#endif

  if (chdir("/") < 0)
    eprintf("chdir:");

  if (argc == 1) {
    cmd = *shell;
    wexecvp_self(cmd, shell);
  } else {
    cmd = argv[1];
    wexecvp_self(cmd, argv + 1);
  }

  savederrno = errno;
  weprintf("wexecvp %s:", cmd);

  _exit(126 + (savederrno == ENOENT));
}
