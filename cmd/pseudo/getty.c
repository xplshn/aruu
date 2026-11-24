/* see LICENSE file for copyright and license details */

#if FEATURE_GTTY_VHANGUP
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "session.h"
#include "util.h"
#include "wexec.h"

static const char *tty;

static void
usage(void)
{
  eprintf("usage: %s [tty] [term] [cmd] [args...]\n", argv0);
}

// ?man getty: set terminal mode
// ?man arguments: tty [term] [cmd] [args ...]
// ?man set terminal line discipline, speed, and mode
int
main(int argc, char *argv[])
{
  char             term[128], logname[LOGIN_NAME_MAX], c;
  char             hostname[HOST_NAME_MAX + 1];
  struct sigaction sa;
  int              fd;
  unsigned int     i = 0;
  ssize_t          n;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND;

  tty = ttyconsole();
  strlcpy(term, ttyconsterm(), sizeof(term));
  if (argc > 0) {
    tty = argv[0];
    if (argc > 1)
      strlcpy(term, argv[1], sizeof(term));
  }

  sa.sa_handler = SIG_IGN;
  sa.sa_flags   = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGHUP, &sa, NULL);

  setenv("TERM", term, 1);

  setsid();

  fd = open(tty, O_RDWR);
  if (fd < 0)
    eprintf("open %s:", tty);
  if (isatty(fd) == 0)
    eprintf("%s is not a tty\n", tty);

  /* steal the controlling terminal if necessary */
  if (ioctl(fd, TIOCSCTTY, (void *)1) != 0)
    weprintf("TIOCSCTTY: could not set controlling tty\n");
#if FEATURE_GTTY_VHANGUP
  vhangup();
#else
  eprintf("getty: vhangup support not built\n");
#endif
  close(fd);

  fd = open(tty, O_RDWR);
  if (fd < 0)
    eprintf("open %s:", tty);
  dup2(fd, 0);
  dup2(fd, 1);
  dup2(fd, 2);
  if (fchown(fd, 0, 0) < 0)
    weprintf("fchown %s:", tty);
  if (fchmod(fd, 0600) < 0)
    weprintf("fchmod %s:", tty);
  if (fd > 2)
    close(fd);

  sa.sa_handler = SIG_DFL;
  sa.sa_flags   = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGHUP, &sa, NULL);

  session_clear(tty);

  if (argc > 2) {
    wexecvp_self(argv[2], argv + 2);
    return 0;
  }

  if (gethostname(hostname, sizeof(hostname)) == 0)
    printf("%s ", hostname);
  printf("login: ");
  fflush(stdout);

  /* flush pending input */
  ioctl(0, TCFLSH, (void *)0);
  memset(logname, 0, sizeof(logname));
  while (1) {
    n = read(0, &c, 1);
    if (n < 0)
      eprintf("read:");
    if (n == 0)
      return 1;
    if (i >= sizeof(logname) - 1)
      eprintf("login name too long\n");
    if (c == '\n' || c == '\r')
      break;
    logname[i++] = c;
  }
  if (logname[0] == '-')
    eprintf("login name cannot start with '-'\n");
  if (logname[0] == '\0')
    return 1;
  wexecvp_self("login", (char *const[]){"login", "-p", logname, NULL});
  return 0;
}
