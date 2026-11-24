/* see LICENSE file for copyright and license details */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s pid...\n", argv0);
}

// ?man pwdx: print working directory of process
// ?man arguments: pid...
// ?man display the current working directory of a process by pid
int
main(int argc, char *argv[])
{
  int  ret = 0;
  char target[PATH_MAX + sizeof(" (deleted)")];

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND;

  if (argc == 0)
    usage();

  for (; argc > 0; argc--, argv++) {
    pid_t pid = (pid_t)estrtonum(*argv, 1, INT_MAX);

    if (proc_cwd(pid, target, sizeof(target)) == 0) {
      printf("%s: %s\n", *argv, target);
      continue;
    }
    if (errno == ENOENT)
      errno = ESRCH;
    weprintf("%s:", *argv);
    ret = 1;
  }

  return ret;
}
