/* see LICENSE file for copyright and license details */

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s file\n", argv0);
}

// ?man sponge: soak up input
// ?man arguments: file
// ?man soak up standard input and write to a file
int
main(int argc, char *argv[])
{
  char tmp[] = "/tmp/sponge-XXXXXX";
  int  fd, tmpfd, ret = 0;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (argc != 1)
    usage();

  if ((tmpfd = mkstemp(tmp)) < 0)
    eprintf("mkstemp:");
  if (unlink(tmp) < 0)
    eprintf("unlink %s:", tmp);
  if (concat(0, "<stdin>", tmpfd, "<tmpfile>") < 0) {
    if (close(tmpfd) < 0)
      weprintf("close tmpfile:");
    return 1;
  }
  if (lseek(tmpfd, 0, SEEK_SET) < 0)
    eprintf("lseek:");

  if ((fd = creat(argv[0], 0666)) < 0)
    eprintf("creat %s:", argv[0]);
  if (concat(tmpfd, "<tmpfile>", fd, argv[0]) < 0)
    ret = 1;
  if (close(tmpfd) < 0) {
    weprintf("close tmpfile:");
    ret = 1;
  }
  if (close(fd) < 0) {
    weprintf("close %s:", argv[0]);
    ret = 1;
  }

  return ret;
}
