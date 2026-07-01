/* See LICENSE file for copyright and license details. */
#include "../sig.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *const aruu_sys_signame[NSIG] = {
    [0] = "EXIT",
#ifdef SIGHUP
    [SIGHUP] = "HUP",
#endif
#ifdef SIGINT
    [SIGINT] = "INT",
#endif
#ifdef SIGQUIT
    [SIGQUIT] = "QUIT",
#endif
#ifdef SIGILL
    [SIGILL] = "ILL",
#endif
#ifdef SIGTRAP
    [SIGTRAP] = "TRAP",
#endif
#ifdef SIGABRT
    [SIGABRT] = "ABRT",
#endif
#ifdef SIGBUS
    [SIGBUS] = "BUS",
#endif
#ifdef SIGFPE
    [SIGFPE] = "FPE",
#endif
#ifdef SIGKILL
    [SIGKILL] = "KILL",
#endif
#ifdef SIGUSR1
    [SIGUSR1] = "USR1",
#endif
#ifdef SIGSEGV
    [SIGSEGV] = "SEGV",
#endif
#ifdef SIGUSR2
    [SIGUSR2] = "USR2",
#endif
#ifdef SIGPIPE
    [SIGPIPE] = "PIPE",
#endif
#ifdef SIGALRM
    [SIGALRM] = "ALRM",
#endif
#ifdef SIGTERM
    [SIGTERM] = "TERM",
#endif
#ifdef SIGCHLD
    [SIGCHLD] = "CHLD",
#endif
#ifdef SIGCONT
    [SIGCONT] = "CONT",
#endif
#ifdef SIGSTOP
    [SIGSTOP] = "STOP",
#endif
#ifdef SIGTSTP
    [SIGTSTP] = "TSTP",
#endif
#ifdef SIGTTIN
    [SIGTTIN] = "TTIN",
#endif
#ifdef SIGTTOU
    [SIGTTOU] = "TTOU",
#endif
#ifdef SIGURG
    [SIGURG] = "URG",
#endif
#ifdef SIGXCPU
    [SIGXCPU] = "XCPU",
#endif
#ifdef SIGXFSZ
    [SIGXFSZ] = "XFSZ",
#endif
#ifdef SIGVTALRM
    [SIGVTALRM] = "VTALRM",
#endif
#ifdef SIGPROF
    [SIGPROF] = "PROF",
#endif
#ifdef SIGWINCH
    [SIGWINCH] = "WINCH",
#endif
#ifdef SIGIO
    [SIGIO] = "IO",
#endif
#ifdef SIGPWR
    [SIGPWR] = "PWR",
#endif
#ifdef SIGSYS
    [SIGSYS] = "SYS",
#endif
};

const int aruu_sys_nsig = NSIG;

int
aruu_sig2str(int signo, char *str)
{
  const char *name;

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)  \
    || defined(__APPLE__)
  if (signo <= 0 || signo >= sys_nsig)
    return -1;
  name = sys_signame[signo];
#else
  if (signo <= 0 || signo >= aruu_sys_nsig)
    return -1;
  name = aruu_sys_signame[signo];
#endif

  if (name == NULL)
    return -1;
  strcpy(str, name);
  return 0;
}

int
aruu_str2sig(const char *str, int *signop)
{
  int                n;
  const char        *s = str;
  const char *const *table;
  int                limit;

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)  \
    || defined(__APPLE__)
  table = sys_signame;
  limit = sys_nsig;
#else
  table = aruu_sys_signame;
  limit = aruu_sys_nsig;
#endif

  if (strncasecmp(s, "SIG", 3) == 0)
    s += 3;

  /* check if it is a number */
  char *ep;
  long  l = strtol(s, &ep, 10);
  if (*s && !*ep) {
    if (l >= 0 && l < limit) {
      *signop = (int)l;
      return 0;
    }
    return -1;
  }

  for (n = 1; n < limit; n++) {
    if (table[n] && strcasecmp(table[n], s) == 0) {
      *signop = n;
      return 0;
    }
  }
  return -1;
}
