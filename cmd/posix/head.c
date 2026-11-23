/* see LICENSE file for copyright and license details */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

// with drop set, everything but the last n lines: a line is held back
// until n others came after it
static void
head(FILE *fp, const char *fname, size_t n, int drop)
{
  char   *buf = NULL, **held = NULL;
  size_t  i = 0, size = 0, nheld = 0, at = 0;
  ssize_t len;

  while ((drop || i < n) && (len = getline(&buf, &size, fp)) > 0) {
    if (!drop) {
      fwrite(buf, 1, len, stdout);
      i += (len && (buf[len - 1] == '\n'));
      continue;
    }
    if (nheld < n) {
      held = ereallocarray(held, nheld + 1, sizeof(*held));
      held[nheld++] = estrdup(buf);
      continue;
    }
    fputs(held[at], stdout);
    free(held[at]);
    held[at] = estrdup(buf);
    at       = (at + 1) % n;
  }
  for (i = 0; i < nheld; i++)
    free(held[i]);
  free(held);
  free(buf);
  if (ferror(fp))
    eprintf("getline %s:", fname);
}

static void
usage(void)
{
  eprintf("usage: %s [-num | -n [-]num] [file ...]\n", argv0);
}

// ?man head: output first part of files
// ?man arguments: file ...
// ?man print the first lines or bytes of files
int
main(int argc, char *argv[])
{
  size_t n = 10;
  FILE  *fp;
  char  *arg;
  int    ret = 0, newline = 0, many = 0, drop = 0;

  ARGBEGIN
  {
    // ?man -n:num: print line numbers or counts
    case 'n':
      arg  = EARGF(usage());
      drop = arg[0] == '-';
      n    = estrtonum(
          arg + drop, 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX)
      );
      break;
    // ?man ARGNUM: specify RGNUM option
    ARGNUM:
      n = ARGNUMF();
      break;
    default:
      usage();
  }
  ARGEND

  // dropping none is printing all
  if (drop && !n) {
    drop = 0;
    n    = SIZE_MAX;
  }

  if (!argc) {
    head(stdin, "<stdin>", n, drop);
  } else {
    many = argc > 1;
    for (newline = 0; *argv; argc--, argv++) {
      if (!strcmp(*argv, "-")) {
        *argv = "<stdin>";
        fp    = stdin;
      } else if (!(fp = fopen(*argv, "r"))) {
        weprintf("fopen %s:", *argv);
        ret = 1;
        continue;
      }
      if (many) {
        if (newline)
          putchar('\n');
        printf("==> %s <==\n", *argv);
      }
      newline = 1;
      head(fp, *argv, n, drop);
      if (fp != stdin && fshut(fp, *argv))
        ret = 1;
    }
  }

  ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

  return ret;
}
