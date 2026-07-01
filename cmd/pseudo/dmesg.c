/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

ssize_t get_dmesg(char *, size_t);
int     clear_dmesg(void);
int     set_console_level(int);

static void
dmesg_show(const void *buf, size_t n)
{
  const char *p = buf;
  ssize_t     r;

  r = write(1, p, n);
  if (r < 0)
    eprintf("write:");
  if (r > 0 && p[r - 1] != '\n')
    putchar('\n');
}

static void
usage(void)
{
  eprintf("usage: %s [-Ccr] [-n level]\n", argv0);
}

// ?man dmesg: print kernel ring buffer
// ?man display or control the kernel ring buffer messages
int
main(int argc, char *argv[])
{
  ssize_t n;
  char   *buf;
  int     cflag = 0;
  long    level;

  ARGBEGIN
  {
    // ?man -C: specify option flag
    case 'C':
      if (clear_dmesg() < 0)
        eprintf("clear_dmesg:");
      return 0;
    // ?man -c: print count or perform stdout action
    case 'c':
      cflag = 1;
      break;
    // ?man -r: operate recursively
    case 'r':
      break;
    // ?man -n:num: print line numbers or counts
    case 'n':
      level = estrtol(EARGF(usage()), 10);
      if (set_console_level(level) < 0)
        eprintf("set_console_level:");
      return 0;
    default:
      usage();
  }
  ARGEND;

  if (argc)
    usage();

  n = get_dmesg(NULL, 0);
  if (n <= 0)
    n = 16384;

  buf = emalloc(n);

  n = get_dmesg(buf, n);
  if (n < 0)
    eprintf("get_dmesg:");

  dmesg_show(buf, n);

  if (cflag && clear_dmesg() < 0)
    eprintf("clear_dmesg:");

  free(buf);

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    return 1;

  return 0;
}