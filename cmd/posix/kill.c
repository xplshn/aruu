/* See LICENSE file for copyright and license details. */

#include "sig.h"
#include "util.h"

#include <sys/wait.h>

#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *
sig2name(int sig)
{
  static char name[SIG2STR_MAX];

  if (sig == 0)
    return "0";
  if (sig2str(sig, name) < 0)
    eprintf("%d: bad signal number\n", sig);
  return name;
}

static int
name2sig(const char *name)
{
  int sig;

  if (strcmp(name, "0") == 0)
    return 0;
  if (str2sig(name, &sig) < 0)
    eprintf("%s: bad signal name\n", name);
  return sig;
}

static void
usage(void)
{
  eprintf(
      "usage: %s [-s signame | -num | -signame] pid ...\n"
      "       %s -l [num]\n",
      argv0,
      argv0
  );
}

// ?man kill: send signal to process
// ?man arguments: pid ...
// ?man send a specified signal to processes by pid
int
main(int argc, char *argv[])
{
  pid_t  pid;
  size_t i;
  int    ret = 0, sig = SIGTERM;

  argv0 = *argv, argv0 ? (argc--, argv++) : (void *)0;

  if (!argc)
    usage();

  if ((*argv)[0] == '-') {
    switch ((*argv)[1]) {
      case 'l':
        if ((*argv)[2])
          goto longopt;
        argc--, argv++;
        if (!argc) {
          for (i = 1; i < (size_t)sys_nsig; i++)
            if (sys_signame[i])
              puts(sys_signame[i]);
        } else if (argc == 1) {
          sig = estrtonum(*argv, 0, INT_MAX);
          if (sig > 128)
            sig = WTERMSIG(sig);
          puts(sig2name(sig));
        } else {
          usage();
        }
        return fshut(stdout, "<stdout>");
      case 's':
        if ((*argv)[2])
          goto longopt;
        argc--, argv++;
        if (!argc)
          usage();
        sig = name2sig(*argv);
        argc--, argv++;
        break;
      case '-':
        if ((*argv)[2])
          goto longopt;
        argc--, argv++;
        break;
      default:
      longopt:
        /* XSI-extensions -argnum and -argname*/
        if (isdigit((*argv)[1])) {
          sig = estrtonum((*argv) + 1, 0, INT_MAX);
          sig2name(sig);
        } else {
          sig = name2sig((*argv) + 1);
        }
        argc--, argv++;
    }
  }

  if (argc && !strcmp(*argv, "--"))
    argc--, argv++;

  if (!argc)
    usage();

  for (; *argv; argc--, argv++) {
    pid = estrtonum(*argv, INT_MIN, INT_MAX);
    if (kill(pid, sig) < 0) {
      weprintf("kill %d:", pid);
      ret = 1;
    }
  }

  return ret;
}
