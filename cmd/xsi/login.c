/* see LICENSE file for copyright and license details */

#include "config.h"
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
#include <time.h>
#include <unistd.h>
#include <utmp.h>

static void
usage(void)
{
  eprintf("usage: %s [-p name] [-f name]\n", argv0);
}

/* reads one line of input, echoing it or not, into a reusable static
 * buffer. returns null on eof so a hung-up line doesnt loop forever */
static char *
readline(const char *prompt, int echo)
{
  static char    buf[LOGIN_NAME_MAX + 1];
  struct termios old, raw;
  int            have_old = 0;
  size_t         n;

  fputs(prompt, stdout);
  fflush(stdout);

  if (!echo && tcgetattr(STDIN_FILENO, &old) == 0) {
    have_old = 1;
    raw      = old;
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

/* best effort utmp record for the controlling tty: getty already
 * cleared any stale entry for this line before exec-ing us. only
 * ut_line, ut_name (musl aliases this to ut_user), and ut_time
 * (aliased to ut_tv.tv_sec) exist on both linux and bsd utmp
 * layouts; ut_type/ut_pid do not exist at all on bsd, so those two
 * fields are the one genuinely platform-specific part of this record */
static void
write_utmp(const char *user)
{
  struct utmp usr;
  char       *line;
  FILE       *fp;

  memset(&usr, 0, sizeof(usr));
#if defined(__linux__)
  usr.ut_type = USER_PROCESS;
  usr.ut_pid  = getpid();
#endif
  line = ttyname(STDIN_FILENO);
  if (line) {
    if (!strncmp(line, "/dev/", 5))
      line += 5;
    strlcpy(usr.ut_line, line, sizeof(usr.ut_line));
  }
  strlcpy(usr.ut_name, user, sizeof(usr.ut_name));
  usr.ut_time = time(NULL);

  /* utmp: a live table, so this overwrites/appends the one entry for
 * this session */
  fp = fopen(UTMP_PATH, "r+");
  if (fp) {
    fseek(fp, 0, SEEK_END);
    fwrite(&usr, sizeof(usr), 1, fp);
    fclose(fp);
  }

  /* wtmp: an ever-growing append-only log of the same record, same
 * portable field subset, just opened differently */
  fp = fopen(WTMP_PATH, "a");
  if (fp) {
    fwrite(&usr, sizeof(usr), 1, fp);
    fclose(fp);
  }
}

// ?man login: authenticate a user and start a session
// ?man arguments: [-p name] [-f name]
// ?man prompt for a username (unless given) and password, verify against
// ?man the system password database, then exec the user's login shell
// ?man invoked by getty with -p name once a username has already been
// ?man typed at the "login:" prompt
int
main(int argc, char *argv[])
{
  struct passwd *pw;
  char           namebuf[LOGIN_NAME_MAX + 1];
  char           shellbuf[PATH_MAX];
  char          *preset = NULL, *trusted = NULL;
  char          *name, *pass, *line, *base;
  char          *shargv[2];
  int            tries;

  ARGBEGIN
  {
    // ?man -p:name preset the username; still prompts for a password
    case 'p':
      preset = EARGF(usage());
      break;
    // ?man -f:name log in as name without a password prompt: the
    // ?man caller has already authenticated this user out-of-band
    case 'f':
      trusted = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND

  pw_init();

  if (trusted) {
    pw = getpwnam(trusted);
    if (!pw)
      eprintf("login: unknown user: %s\n", trusted);
  } else {
    pw = NULL;
    for (tries = 0; tries < 3 && !pw; tries++) {
      if (preset && tries == 0) {
        strlcpy(namebuf, preset, sizeof(namebuf));
      } else {
        line = readline("login: ", 1);
        if (!line)
          exit(1);
        strlcpy(namebuf, line, sizeof(namebuf));
      }
      name = namebuf;

      pass = readline("Password: ", 0);
      if (!pass)
        pass = "";

      pw = getpwnam(name);
      if (!pw || pw_check(pw, pass) != 1) {
        fputs("Login incorrect\n", stdout);
        pw = NULL;
      }
    }
    if (!pw)
      exit(1);
  }

  if (initgroups(pw->pw_name, pw->pw_gid) < 0)
    weprintf("initgroups:");
  if (setgid(pw->pw_gid) < 0)
    eprintf("setgid:");
  if (setuid(pw->pw_uid) < 0)
    eprintf("setuid:");

  write_utmp(pw->pw_name);

  if (chdir(pw->pw_dir) < 0)
    weprintf("chdir %s:", pw->pw_dir);

  setenv("HOME", pw->pw_dir, 1);
  setenv("USER", pw->pw_name, 1);
  setenv("LOGNAME", pw->pw_name, 1);
  strlcpy(shellbuf, pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", sizeof(shellbuf));
  setenv("SHELL", shellbuf, 1);

  base = strrchr(shellbuf, '/');
  base = base ? base + 1 : shellbuf;

  shargv[0]    = emalloc(strlen(base) + 2);
  shargv[0][0] = '-';
  strcpy(shargv[0] + 1, base);
  shargv[1] = NULL;

  wexecv_self(shellbuf, shargv);
  return 1;
}
