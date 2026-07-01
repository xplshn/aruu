/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "config.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-ml]\n", argv0);
}

// ?man who: show logged in users
// ?man display a list of users currently logged into the system
int
main(int argc, char *argv[])
{
  struct utmp usr;
  FILE       *ufp;
  char        timebuf[sizeof "yyyy-mm-dd hh:mm"];
  char        line_buf[sizeof(usr.ut_line) + 1];
  char        name_buf[sizeof(usr.ut_name) + 1];
  char       *tty, *ttmp;
  int         mflag = 0, lflag = 0;
  time_t      t;

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

  if (!(ufp = fopen(UTMP_PATH, "r")))
    eprintf("fopen: %s:", UTMP_PATH);

  while (fread(&usr, sizeof(usr), 1, ufp) == 1) {
    memcpy(line_buf, usr.ut_line, sizeof(usr.ut_line));
    line_buf[sizeof(usr.ut_line)] = '\0';
    memcpy(name_buf, usr.ut_name, sizeof(usr.ut_name));
    name_buf[sizeof(usr.ut_name)] = '\0';

    if (!*name_buf || !*line_buf || line_buf[0] == '~')
      continue;
    if (mflag != 0 && strcmp(line_buf, tty) != 0)
      continue;
    if (!!strcmp(name_buf, "LOGIN") == lflag)
      continue;
    t = usr.ut_time;
    strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M", localtime(&t));
    printf("%-8s %-12s %-16s\n", name_buf, line_buf, timebuf);
  }
  fclose(ufp);
  return 0;
}
