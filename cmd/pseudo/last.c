/* see LICENSE file for copyright and license details */

#include <errno.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "paths.h"
#include "session.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [user]\n", argv0);
}

static int
show(const struct SessionEntry *e, void *arg)
{
  const char *user = arg;
  time_t      t;

  if (!e->login || (user && strcmp(user, e->user)))
    return 0;
  t = e->time;
  printf("%-8.8s %-8.8s %-16.16s %s", e->user, e->line, e->host, ctime(&t));
  return 0;
}

// ?man last: show last logged in users
// ?man arguments: user
// ?man display a list of recent user logins
int
main(int argc, char **argv)
{
  char *user = NULL, *file, *prog;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND;

  switch (argc) {
    case 0:
      user = NULL;
      break;
    case 1:
      user = argv[0];
      break;
    default:
      usage();
  }

  prog = basename(argv0);
  file = (!strcmp(prog, "last")) ? WTMP_PATH : BTMP_PATH;
  if (session_each(file, show, user) < 0)
    eprintf("fopen %s:", file);
  return 0;
}
