/* see LICENSE file for copyright and license details */
#include "passwd.h"
#include "util.h"

#include <crypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf("usage: %s [password]\n", argv0);
}

// ?man mkpasswd: generate a crypt(3) password hash
// ?man arguments: [password]
// ?man hash a password for use in /etc/passwd or /etc/shadow. reads
// ?man the password from the command line, or prompts for it if not given
int
main(int argc, char *argv[])
{
  static char    buf[128];
  struct termios old, raw;
  char           salt[PW_SALT_MAX];
  char          *pass, *hash;
  int            have_old;
  size_t         n;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (argc > 0) {
    pass = argv[0];
  } else {
    fputs("Password: ", stdout);
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
      exit(1);
    }

    if (have_old) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
      fputc('\n', stdout);
    }

    n = strlen(buf);
    if (n && buf[n - 1] == '\n')
      buf[--n] = '\0';
    pass = buf;
  }

  pw_init();
  if (pw_gensalt(salt, sizeof(salt)) < 0)
    eprintf("mkpasswd: cannot generate salt\n");

  hash = crypt(pass, salt);
  if (!hash)
    eprintf("mkpasswd: crypt:");

  printf("%s\n", hash);
  return 0;
}
