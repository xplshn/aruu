/* see LICENSE file for copyright and license details */
#include "passwd.h"
#include "util.h"
#include "wexec.h"

#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf("usage: %s [-l] [name]\n", argv0);
}

/* reads one line with echo off into a reusable static buffer */
static char *
readpass(const char *prompt)
{
  static char    buf[128];
  struct termios old, raw;
  int            have_old;
  size_t         n;

  fputs(prompt, stdout);
  fflush(stdout);

  have_old = tcgetattr(STDIN_FILENO, &old) == 0;
  if (have_old) {
    raw = old;
    raw.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }

  if (!fgets(buf, sizeof(buf), stdin)) {
    if (have_old)
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    return NULL;
  }

  if (have_old) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    fputc('\n', stdout);
  }

  n = strlen(buf);
  if (n && buf[n - 1] == '\n')
    buf[--n] = '\0';

  return buf;
}

// ?man su: become another user
// ?man arguments: [-l] [name]
// ?man authenticate as name (default: root) and start a shell as them
// ?man with -l, also chdir to their home directory and run a login shell,
// ?man same as login would
int
main(int argc, char *argv[])
{
  struct passwd *pw;
  char           shellbuf[PATH_MAX];
  char          *name, *pass, *base;
  char          *shargv[2];
  int            lflag = 0;
  uid_t          uid;

  ARGBEGIN
  {
    // ?man -l: start a login shell, chdir home like login does
    case 'l':
      lflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  name = argc > 0 ? argv[0] : "root";
  pw   = getpwnam(name);
  if (!pw)
    eprintf("su: unknown user: %s\n", name);

  uid = getuid();
  if (uid != 0) {
    pass = readpass("Password: ");
    if (!pass || pw_check(pw, pass) != 1)
      eprintf("su: incorrect password\n");
  }

  if (initgroups(pw->pw_name, pw->pw_gid) < 0)
    weprintf("initgroups:");
  if (setgid(pw->pw_gid) < 0)
    eprintf("setgid:");
  if (setuid(pw->pw_uid) < 0)
    eprintf("setuid:");

  strlcpy(shellbuf, pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", sizeof(shellbuf));

  if (lflag) {
    if (chdir(pw->pw_dir) < 0)
      weprintf("chdir %s:", pw->pw_dir);
    setenv("HOME", pw->pw_dir, 1);
  }
  setenv("USER", pw->pw_name, 1);
  setenv("LOGNAME", pw->pw_name, 1);
  setenv("SHELL", shellbuf, 1);

  base = strrchr(shellbuf, '/');
  base = base ? base + 1 : shellbuf;

  if (lflag) {
    shargv[0]    = emalloc(strlen(base) + 2);
    shargv[0][0] = '-';
    strcpy(shargv[0] + 1, base);
  } else {
    shargv[0] = estrdup(base);
  }
  shargv[1] = NULL;

  wexecv_self(shellbuf, shargv);
  return 1;
}
