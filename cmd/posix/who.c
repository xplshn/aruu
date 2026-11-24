/* see LICENSE file for copyright and license details */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "session.h"
#include "util.h"

static int   mflag, lflag;
static char *tty;

static void
usage(void)
{
  eprintf("usage: %s [-ml]\n", argv0);
}

static int
show(const struct SessionEntry *e, void *arg)
{
  char   timebuf[sizeof "yyyy-mm-dd hh:mm"];
  time_t t;

  (void)arg;
  if (!*e->user || !*e->line || e->line[0] == '~')
    return 0;
  if (mflag != 0 && strcmp(e->line, tty) != 0)
    return 0;
  if (!!strcmp(e->user, "LOGIN") == lflag)
    return 0;
  t = e->time;
  strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M", localtime(&t));
  printf("%-8s %-12s %-16s\n", e->user, e->line, timebuf);
  return 0;
}

// ?man who: show logged in users
// ?man display a list of users currently logged into the system
int
main(int argc, char *argv[])
{
  char *ttmp;

  ARGBEGIN
  {
    // ?man -m: specify mode or limit
    case 'm':
      mflag = 1;
      tty   = ttyname(0);
      if (!tty)
        eprintf("ttyname: stdin:");
      if ((ttmp = strrchr(tty, '/')))
        tty = ttmp + 1;
      break;
    // ?man -l: list in long format
    case 'l':
      lflag = 1;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc > 0)
    usage();

  if (session_each(UTMP_PATH, show, NULL) < 0)
    eprintf("fopen: %s:", UTMP_PATH);
  return 0;
}
