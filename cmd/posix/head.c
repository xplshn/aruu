/* see LICENSE file for copyright and license details */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

static void
head(FILE *fp, const char *fname, size_t n)
{
  char   *buf = NULL;
  size_t  i = 0, size = 0;
  ssize_t len;

  while (i < n && (len = getline(&buf, &size, fp)) > 0) {
    fwrite(buf, 1, len, stdout);
    i += (len && (buf[len - 1] == '\n'));
  }
  free(buf);
  if (ferror(fp))
    eprintf("getline %s:", fname);
}

#if FEATURE_HEAD_NEGATIVE
// gnu extension, -n -N: everything but the last n lines. a line is held
// back until n others came after it
static void
head_but(FILE *fp, const char *fname, size_t n)
{
  char   *buf = NULL, **held = NULL;
  size_t  i, size = 0, nheld = 0, at = 0;
  ssize_t len;

  while ((len = getline(&buf, &size, fp)) > 0) {
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
#endif

static void
usage(void)
{
  eprintf("usage: %s [-num | -n num] [file ...]\n", argv0);
}

// ?man head: output first part of files
// ?man arguments: file ...
// ?man print the first lines or bytes of files
int
main(int argc, char *argv[])
{
  size_t n = 10;
  FILE  *fp;
  int    ret = 0, newline = 0, many = 0;
#if FEATURE_HEAD_NEGATIVE
  char  *arg;
  int    drop = 0;
#endif

  ARGBEGIN
  {
    // ?man -n:num: print line numbers or counts
    case 'n':
#if FEATURE_HEAD_NEGATIVE
      arg  = EARGF(usage());
      drop = arg[0] == '-' && arg[1];
      n    = estrtonum(
          arg + drop, 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX)
      );
#else
      n = estrtonum(
          EARGF(usage()), 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SIZE_MAX)
      );
#endif
      break;
    // ?man ARGNUM: specify RGNUM option
    ARGNUM:
      n = ARGNUMF();
      break;
    default:
      usage();
  }
  ARGEND

#if FEATURE_HEAD_NEGATIVE
  // dropping none is printing all
  if (drop && !n) {
    drop = 0;
    n    = SIZE_MAX;
  }
#endif

  if (!argc) {
#if FEATURE_HEAD_NEGATIVE
    if (drop)
      head_but(stdin, "<stdin>", n);
    else
#endif
      head(stdin, "<stdin>", n);
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
#if FEATURE_HEAD_NEGATIVE
      if (drop)
        head_but(fp, *argv, n);
      else
#endif
        head(fp, *argv, n);
      if (fp != stdin && fshut(fp, *argv))
        ret = 1;
    }
  }

  ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

  return ret;
}
