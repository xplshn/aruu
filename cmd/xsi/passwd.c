/* see LICENSE file for copyright and license details */
#include "passwd.h"
#include "util.h"

#include <crypt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf("usage: %s [-l | -u | -d] [name]\n", argv0);
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

// ?man passwd: change a user's password
// ?man arguments: [-l | -u | -d] [name]
// ?man change name's password (default: the caller's own), or with -l/-u/-d
// ?man lock, unlock, or clear it instead. only root may target another
// ?man user, or use -l/-u/-d at all
int
main(int argc, char *argv[])
{
  struct pwdb_entry  ent;
  struct passwd     *self, *target;
  char              *name, *cur, *new1, *new2, *hash;
  char               salt[PW_SALT_MAX];
  int                lflag = 0, uflag = 0, dflag = 0;
  uid_t              uid;

  ARGBEGIN
  {
    // ?man -l: lock the account by prefixing its hash with '!'
    case 'l':
      lflag = 1;
      break;
    // ?man -u: unlock an account previously locked with -l
    case 'u':
      uflag = 1;
      break;
    // ?man -d: clear the password, allowing a blank password login
    case 'd':
      dflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (lflag + uflag + dflag > 1)
    usage();

  uid  = getuid();
  self = getpwuid(uid);
  if (!self)
    eprintf("passwd: who are you?\n");

  name = argc > 0 ? argv[0] : self->pw_name;
  if (uid != 0 && (strcmp(name, self->pw_name) != 0 || lflag || uflag || dflag))
    eprintf("passwd: permission denied\n");

  target = getpwnam(name);
  if (!target)
    eprintf("passwd: unknown user: %s\n", name);

  pw_init();

  if (lflag || uflag || dflag) {
    if (pwdb_lookup(&ent, name) < 0)
      eprintf("passwd: cannot look up %s\n", name);

    if (lflag) {
      if (ent.hash[0] == '!') {
        hash = ent.hash;
      } else {
        hash    = emalloc(strlen(ent.hash) + 2);
        hash[0] = '!';
        strcpy(hash + 1, ent.hash);
      }
      if (pwdb_update(name, hash) < 0)
        eprintf("passwd: update failed\n");
      printf("passwd: password for %s locked\n", name);
    } else if (uflag) {
      hash = ent.hash[0] == '!' ? ent.hash + 1 : ent.hash;
      if (pwdb_update(name, hash) < 0)
        eprintf("passwd: update failed\n");
      printf("passwd: password for %s unlocked\n", name);
    } else {
      if (pwdb_update(name, "") < 0)
        eprintf("passwd: update failed\n");
      printf("passwd: password for %s cleared\n", name);
    }
    return 0;
  }

  if (uid != 0) {
    cur = readpass("Current password: ");
    if (!cur || pw_check(self, cur) != 1)
      eprintf("passwd: authentication failed\n");
  }

  new1 = readpass("New password: ");
  if (!new1)
    exit(1);
  new1 = estrdup(new1);

  new2 = readpass("Retype new password: ");
  if (!new2 || strcmp(new1, new2) != 0) {
    free(new1);
    eprintf("passwd: passwords do not match\n");
  }
  if (new1[0] == '\0')
    weprintf("passwd: warning: empty password\n");

  if (pw_gensalt(salt, sizeof(salt)) < 0)
    eprintf("passwd: cannot generate salt\n");

  hash = crypt(new1, salt);
  free(new1);
  if (!hash)
    eprintf("passwd: crypt:");

  if (pwdb_update(name, hash) < 0)
    eprintf("passwd: update failed\n");

  printf("passwd: password for %s updated\n", name);
  return 0;
}
