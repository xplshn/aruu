/* See LICENSE file for copyright and license details. */
#include "passwd.h"
#include "config.h"
#include "text.h"
#include "util.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf("usage: %s [username]\n", argv0);
}

// ?man passwd: change a user password
// ?man arguments: [username]
// ?man change the password associated with the calling user or with username
int
main(int argc, char *argv[])
{
  struct pwdb_entry ent;
  struct passwd    *pw;
  char             *inpass, *prevhash = NULL, *newhash = NULL, salt[PW_SALT_MAX];
  char             *c1, *c2;
  int               status = 1;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  pw_init();
  umask(077);

  if (argc == 0)
    pw = getpwuid(getuid());
  else
    pw = getpwnam(argv[0]);
  if (!pw) {
    if (errno)
      eprintf("getpwnam: %s:", argv[0]);
    else
      eprintf("who are you?\n");
  }

  if (pwdb_lookup(&ent, pw->pw_name) < 0)
    return 1;
  prevhash = ent.hash;

  if (getuid() != 0) {
    if (prevhash[0] == '!' || prevhash[0] == '*')
      eprintf("denied\n");
    if (prevhash[0] == '\0') {
      /* no password set */
    } else {
      printf("Changing password for %s\n", pw->pw_name);
      inpass = getpass("Old password: ");
      if (!inpass)
        eprintf("getpass:");
      if (inpass[0] == '\0')
        eprintf("no password supplied\n");
      c1 = crypt(inpass, prevhash);
      if (!c1)
        eprintf("crypt:");
      if (strcmp(c1, prevhash) != 0)
        eprintf("incorrect password\n");
      explicit_bzero(inpass, strlen(inpass));
    }
  }

  inpass = getpass("Enter new password: ");
  if (!inpass)
    eprintf("getpass:");
  if (inpass[0] == '\0')
    eprintf("no password supplied\n");

  if (prevhash && prevhash[0] != '\0') {
    c1 = crypt(inpass, prevhash);
    if (c1 && strcmp(c1, prevhash) == 0)
      eprintf("password left unchanged\n");
  }

  if (pw_gensalt(salt, sizeof(salt)) < 0)
    eprintf("pw_gensalt:");
  c1 = crypt(inpass, salt);
  if (!c1)
    eprintf("crypt:");
  newhash = estrdup(c1);
  explicit_bzero(inpass, strlen(inpass));

  inpass = getpass("Retype new password: ");
  if (!inpass)
    eprintf("getpass:");
  if (inpass[0] == '\0')
    eprintf("no password supplied\n");
  c2 = crypt(inpass, salt);
  if (!c2)
    eprintf("crypt:");
  if (strcmp(c2, newhash) != 0)
    eprintf("passwords don't match\n");
  explicit_bzero(inpass, strlen(inpass));

  if (pwdb_update(pw->pw_name, newhash) == 0)
    status = 0;

  free(newhash);
  free(ent.name);
  free(ent.hash);
  return status;
}
