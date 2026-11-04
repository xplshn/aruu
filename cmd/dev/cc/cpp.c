/* see license file for copyright and license details */
#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include "arg.h"
#include "cc.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
  fprintf(
      stderr,
      "usage: %s [-D name[=val]] [-U name] [-I dir] [-o out] [file "
      "...]\n",
      argv0
  );
  exit(1);
}

int
main(int argc, char *argv[])
{
  char  *output = NULL;
  char  *arg;
  char  *cmdline_buf = NULL;
  size_t cmdline_len = 0;
  size_t cmdline_cap = 0;
  bool   undef       = false;
  int    i;

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-undef") == 0)
      undef = true;
  }

  argv0 = progname(argv[0], "cpp");
  if (!undef) {
    char   predefs[2048];
    char  *p = predefs;
    size_t predefs_len;

    p += sprintf(p, "#define __LP64__ 1\n");
    p += sprintf(p, "#define __STDC_NO_ATOMICS__ 1\n");
    p += sprintf(p, "#define __STDC_NO_COMPLEX__ 1\n");
    p += sprintf(p, "#define __SIZEOF_LONG__ 8\n");
    p += sprintf(p, "#define __SIZEOF_POINTER__ 8\n");
    p += sprintf(p, "#define __SIZEOF_SIZE_T__ 8\n");
    p += sprintf(p, "#define __SIZEOF_PTRDIFF_T__ 8\n");
    p += sprintf(p, "#define __SIZE_TYPE__ unsigned long\n");
    p += sprintf(p, "#define __PTRDIFF_TYPE__ long\n");
    p += sprintf(p, "#define __WCHAR_TYPE__ int\n");
    p += sprintf(p, "#define __WINT_TYPE__ unsigned int\n");
    p += sprintf(p, "#define __INTMAX_TYPE__ long\n");
    p += sprintf(p, "#define __UINTMAX_TYPE__ unsigned long\n");

#if defined(__x86_64__) || defined(__amd64__)
    p += sprintf(p, "#define __x86_64__ 1\n");
    p += sprintf(p, "#define __amd64__ 1\n");
#elif defined(__aarch64__)
    p += sprintf(p, "#define __aarch64__ 1\n");
#elif defined(__riscv) || defined(__riscv__)
    p += sprintf(p, "#define __riscv 1\n");
#endif

#if defined(__linux__)
    p += sprintf(p, "#define __linux__ 1\n");
    p += sprintf(p, "#define __unix__ 1\n");
#elif defined(__FreeBSD__)
    p += sprintf(p, "#define __FreeBSD__ 1\n");
    p += sprintf(p, "#define __unix__ 1\n");
#elif defined(__NetBSD__)
    p += sprintf(p, "#define __NetBSD__ 1\n");
    p += sprintf(p, "#define __unix__ 1\n");
#elif defined(__OpenBSD__)
    p += sprintf(p, "#define __OpenBSD__ 1\n");
    p += sprintf(p, "#define __unix__ 1\n");
#elif defined(__APPLE__)
    p += sprintf(p, "#define __APPLE__ 1\n");
    p += sprintf(p, "#define __unix__ 1\n");
#elif defined(__unix__)
    p += sprintf(p, "#define __unix__ 1\n");
#endif

    predefs_len = p - predefs;
    cmdline_cap = predefs_len + 2048;
    cmdline_buf = xreallocarray(cmdline_buf, cmdline_cap, 1);
    strcpy(cmdline_buf, predefs);
    cmdline_len = predefs_len;
  }
  ARGBEGIN
  {
    case 'u':
      if (strcmp(opt_, "undef") == 0) {
        done_ = 1;
        break;
      }
      usage();
      break;
    case 'D':
      arg = EARGF(usage());
      {
        char *eq = strchr(arg, '=');
        char  line[1024];
        if (eq) {
          *eq = '\0';
          snprintf(line, sizeof(line), "#define %s %s\n", arg, eq + 1);
          *eq = '=';
        } else {
          snprintf(line, sizeof(line), "#define %s 1\n", arg);
        }
        size_t len = strlen(line);
        if (cmdline_len + len + 1 > cmdline_cap) {
          cmdline_cap = cmdline_cap ? cmdline_cap * 2 : 2048;
          cmdline_buf = xreallocarray(cmdline_buf, cmdline_cap, 1);
        }
        strcpy(cmdline_buf + cmdline_len, line);
        cmdline_len += len;
      }
      break;
    case 'U':
      arg = EARGF(usage());
      {
        char line[1024];
        snprintf(line, sizeof(line), "#undef %s\n", arg);
        size_t len = strlen(line);
        if (cmdline_len + len + 1 > cmdline_cap) {
          cmdline_cap = cmdline_cap ? cmdline_cap * 2 : 2048;
          cmdline_buf = xreallocarray(cmdline_buf, cmdline_cap, 1);
        }
        strcpy(cmdline_buf + cmdline_len, line);
        cmdline_len += len;
      }
      break;
    case 'n':
      if (strcmp(opt_, "nostdinc") == 0) {
        done_ = 1;
        break;
      }
      usage();
      break;
    case 'i':
      if (strcmp(opt_, "isystem") == 0 || strcmp(opt_, "iquote") == 0
          || strcmp(opt_, "idirafter") == 0) {
        if (!--argc)
          usage();
        addincdir(*++argv);
        done_ = 1;
        break;
      }
      usage();
      break;
    case 'I':
      arg = EARGF(usage());
      addincdir(arg);
      break;
    case 'P':
      if (strcmp(opt_, "P") == 0) {
        done_ = 1;
        break;
      }
      usage();
      break;
    case 's':
      if (strncmp(opt_, "std=", 4) == 0) {
        done_ = 1;
        break;
      }
      usage();
      break;
    case 'o':
      output = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND

  targinit(NULL);
  if (output && !freopen(output, "w", stdout))
    fatal("open %s:", output);

  if (argc) {
    while (argc--)
      scanfrom(argv[argc], NULL);
  } else {
    scanfrom("<stdin>", stdin);
  }

  if (cmdline_len > 0) {
    FILE *f = fmemopen(cmdline_buf, cmdline_len, "r");
    if (!f)
      fatal("fmemopen failed");
    scanfrom("<cmdline>", f);
  }

  scanopen();

  tokeninit();
  ppinit();
  ppflags |= PPNEWLINE;
  while (tok.kind != TEOF) {
    tokenprint(&tok, stdout);
    next();
  }

  fflush(stdout);
  if (ferror(stdout))
    fatal("write failed");

  free(cmdline_buf);
  return 0;
}
