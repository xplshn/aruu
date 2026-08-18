/* see LICENSE file for copyright and license details */

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-ai] [file ...]\n", argv0);
}

// ?man tee: duplicate input
// ?man arguments: file ...
// ?man read from standard input and write to standard output and files
int
main(int argc, char *argv[])
{
  int    *fds = NULL;
  size_t  i, nfds;
  ssize_t n;
  int     ret = 0, aflag = O_TRUNC, iflag = 0;
  char    buf[BUFSIZ];

  ARGBEGIN
  {
    // ?man -a: print or show all entries
    case 'a':
      aflag = O_APPEND;
      break;
    // ?man -i: interactive mode or prompt for confirmation
    case 'i':
      iflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (iflag && signal(SIGINT, SIG_IGN) == SIG_ERR)
    eprintf("signal:");
  nfds = argc + 1;
  fds  = ecalloc(nfds, sizeof(*fds));

  for (i = 0; i < (size_t)argc; i++) {
    if ((fds[i] = open(argv[i], O_WRONLY | O_CREAT | aflag, 0666)) < 0) {
      weprintf("open %s:", argv[i]);
      ret = 1;
    }
  }
  fds[i] = 1;

  while ((n = read(0, buf, sizeof(buf))) > 0) {
    for (i = 0; i < nfds; i++) {
      if (fds[i] >= 0 && writeall(fds[i], buf, n) < 0) {
        weprintf("write %s:", (i != (size_t)argc) ? argv[i] : "<stdout>");
        fds[i] = -1;
        ret    = 1;
      }
    }
  }
  if (n < 0)
    eprintf("read <stdin>:");

  return ret;
}
