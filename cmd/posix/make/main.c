#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "make.h"

#ifndef SIGINT
#define SIGINT -1
#endif

#ifndef SIGTERM
#define SIGTERM -1
#endif

#ifndef SIGQUIT
#define SIGQUIT -1
#endif

#ifndef SIGHUP
#define SIGHUP -1
#endif

int                   kflag, dflag, nflag, iflag, sflag;
int                   eflag, pflag, tflag, qflag;
int                   exitstatus;
volatile sig_atomic_t stop;

void
debug(char *fmt, ...)
{
  va_list va;

  if (!dflag)
    return;

  va_start(va, fmt);
  vfprintf(stdout, fmt, va);
  fputc('\n', stdout);
  va_end(va);
}

int
hash(char *name)
{
  int      c;
  unsigned h = 5381;

  while ((c = *name++))
    h = h * 33 ^ c;

  return h;
}

void
sighandler(int signo)
{
  killchild();
  stop = signo;
}

static void
usage(void)
{
  fputs(
      "usage: make [-eiknprSstd] [-f file] [-j jobs] "
      "[macro=value ...] [target ...]\n",
      stderr
  );
  exit(EXIT_FAILURE);
}

static char *
getarg(char **args, char ***argv)
{
  char *s;

  if ((*args)[1]) {
    s = (*args) + 1;
    *args += strlen(*args) - 1;
    return s;
  }

  if (!argv)
    usage();

  if ((*argv)[1] == NULL)
    usage();
  (*argv)++;

  return **argv;
}

// MAKEFLAGS is a list of words, so a space inside one, as in CFLAGS=-O2 -g,
// goes in behind a backslash, and so does a backslash
static void
appendmakeflags(char *text)
{
  size_t n;
  char  *s, *t, *w;

  s = getmacro("MAKEFLAGS");
  n = strlen(s);
  t = emalloc(n + 2 * strlen(text) + 2);
  memcpy(t, s, n);
  w = t + n;
  if (n)
    *w++ = ' ';
  for (; *text; text++) {
    if (*text == ' ' || *text == '\t' || *text == '\\')
      *w++ = '\\';
    *w++ = *text;
  }
  *w = '\0';
  setmacro("MAKEFLAGS", t, MAKEFLAGS, EXPORT);

  free(t);
}

// the next word of a MAKEFLAGS list, cut out in place and its escapes
// undone. NULL at the end
static char *
flagword(char **list)
{
  char *start, *w;

  while (**list == ' ' || **list == '\t')
    (*list)++;
  if (!**list)
    return NULL;
  start = w = *list;
  while (**list && **list != ' ' && **list != '\t') {
    if (**list == '\\' && (*list)[1])
      (*list)++;
    *w++ = *(*list)++;
  }
  if (**list)
    (*list)++;
  *w = '\0';
  return start;
}

static int
hasargs(int c)
{
  return c == 'f' || c == 'j';
}

// gmake compat: a bare -j, with nothing after it or something that is
// not a count, asks for as many jobs as there can be and takes no argument
static int
takesarg(int c, char **args, char ***argv)
{
#if FEATURE_MAKE_GMAKE
  if (c == 'j') {
    if ((*args)[1])
      return 1;
    return argv && (*argv)[1] && isdigit((unsigned char)(*argv)[1][0]);
  }
#else
  (void)args;
  (void)argv;
#endif
  return hasargs(c);
}

static void
parseflag(int flag, char **args, char ***argv)
{
  if (takesarg(flag, args, argv))
    getarg(args, argv);

  switch (flag) {
    case 'j':
      /* ?man -j:jobs: number of parallel jobs (accepted, runs
 * serially) */
    case 'f':
      /* ?man -f:file: read rules from file instead of makefile or
 * makefile */
      break;
    case 'e':
      /* ?man -e: environment variables override makefile assignments
 */
      eflag = 1;
      appendmakeflags("-e");
      break;
    case 'i':
      /* ?man -i: ignore errors from executed commands */
      iflag = 1;
      appendmakeflags("-i");
      break;
    case 'k':
      /* ?man -k: continue building other targets after an error */
      kflag = 1;
      appendmakeflags("-k");
      break;
    case 'n':
      /* ?man -n: print commands without executing them */
      nflag = 1;
      appendmakeflags("-n");
      break;
    case 'p':
      /* ?man -p: print the rule and macro database then exit */
      pflag = 1;
      break;
    case 'q':
      /* ?man -q: question mode, return zero if up to date, one
 * otherwise */
      qflag = 1;
      appendmakeflags("-q");
      break;
    case 'r':
      /* ?man -r: clear the built-in suffix rules and variables */
      addtarget(".SUFFIXES", 0);
      appendmakeflags("-r");
      break;
    case 'S':
      /* ?man -S: cancel a previous -k, stop on first error */
      kflag = 0;
      appendmakeflags("-S");
      break;
    case 's':
      /* ?man -s: silent, do not echo commands before running them */
      sflag = 1;
      appendmakeflags("-s");
      break;
    case 't':
      /* ?man -t: touch targets instead of rebuilding them */
      tflag = 1;
      appendmakeflags("-t");
      break;
    case 'd':
      /* ?man -d: enable debug output */
      dflag = 1;
      appendmakeflags("-d");
      break;
    default:
      usage();
  }
}

static int
assign(char *s, int where, int export)
{
  int   pos;
  char *t;

  if ((t = strchr(s, '=')) == NULL)
    return 0;

  pos = t - s;

  appendmakeflags(s);
  t      = estrdup(s);
  t[pos] = '\0';

  setmacro(t, t + pos + 1, where, export);
  free(t);
  return 1;
}

// macros may come after targets too, as they do in "make all CC=cc", so a
// command line is read to its end and what is not an option or a macro is a
// target. the options of MAKEFLAGS have no targets to collect
static void
parseargv(char **argv, char ***targets, int where, int export)
{
  char  *s;
  char  *hm = NULL;
  char **found = NULL;
  int    n = 0;

  for (; *argv; ++argv) {
    s = *argv;
    if (hm == NULL && strcmp(s, "--") == 0) {
      hm = *argv;
    } else if (hm && s[0] == '-') {
      break;
    } else if (s[0] != '-') {
      if (assign(s, where, export))
        continue;
      if (!targets)
        break;
      found       = erealloc(found, (n + 2) * sizeof(*found));
      found[n++]  = s;
      found[n]    = NULL;
      continue;
    }
    while (hm == NULL && *++s)
      parseflag(*s, &s, &argv);
  }

  if (targets) {
    if (!found) {
      found    = emalloc(sizeof(*found));
      found[0] = NULL;
    }
    *targets = found;
  }
}

static void
parsemakeflags(void)
{
  int   n;
  char *s, *flags, **arr;

  if ((flags = getenv("MAKEFLAGS")) == NULL)
    return;

  setmacro("MAKEFLAGS", "", MAKEFLAGS, EXPORT);

  while (*flags == ' ' || *flags == '\t')
    flags++;

  if (flags[0] != '-' && !strchr(flags, '=')) {
    while (*flags) {
      parseflag(*flags, &flags, NULL);
      flags++;
    }
  } else {
    n   = 0;
    arr = NULL;
    while ((s = flagword(&flags))) {
      n++;
      arr        = erealloc(arr, sizeof(char *) * (n + 1));
      arr[n - 1] = s;
      arr[n]     = NULL;
    }

    if (arr)
      parseargv(arr, NULL, MAKEFLAGS, NOEXPORT);
    free(arr);
  }
}

static void
parsemakefiles(char **argv)
{
  char *s, *arg;
  int   c, hasmake;

  hasmake = 0;
  for (; *argv; ++argv) {
    if (**argv != '-')
      continue;
    for (s = *argv; (c = *s); ++s) {
      if (takesarg(c, &s, &argv))
        arg = getarg(&s, &argv);

      if (c == 'f') {
        if (strcmp(arg, "-") == 0)
          arg = NULL;
        parse(arg);
        hasmake = 1;
      }
    }
  }

  if (hasmake)
    return;

  if (parse("makefile"))
    return;
  if (parse("Makefile"))
    return;
}

/*
 * we want to enable debug as earlier as possible,
 * if we wait until we read the makefiles then
 * we are going to lose to much debug information
 */
static void
enadebug(char *argv[])
{
  int   c;
  char *p;

  for (; *argv && **argv == '-'; ++argv) {
    p = *argv;
    for (++p; (c = *p); ++p) {
      if (takesarg(c, &p, &argv))
        getarg(&p, &argv);
      if (c == 'd')
        dflag = 1;
    }
  }
}

// ?man make: maintain program dependencies
// ?man arguments: [name=value ...] [target ...]
// ?man update files based on dependency rules in a makefile
// ?man if no -f is given, make tries makefile then Makefile
// ?man if no target is given the first non-dot target in the makefile is built
int
main(int argc, char *argv[])
{
  char *arg0, **targets;

  (void)argc;

  signal(SIGINT, sighandler);
  signal(SIGHUP, sighandler);
  signal(SIGTERM, sighandler);
  signal(SIGQUIT, sighandler);

  targets = NULL;
  arg0    = *argv++;

  enadebug(argv);
  inject(defaults);
  setmacro("MAKE", arg0, MAKEFILE, NOEXPORT);

  parsemakeflags();
  parseargv(argv, &targets, CMDLINE, EXPORT);
  parsemakefiles(argv);

  if (pflag) {
    dumpmacros();
    dumprules();
    exit(EXIT_SUCCESS);
  }

  if (!*targets) {
    build(NULL);
  } else {
    while (*targets)
      build(*targets++);
  }

  exit(exitstatus);

  return 0;
}
