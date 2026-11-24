/* see LICENSE file for copyright and license details */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../config.h"
#include "../session.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

/* getty already cleared any stale entry for this line before exec-ing
 * login. ut_name is ut_user and ut_time is ut_tv.tv_sec under musl */
void
session_record(const char *user)
{
  struct utmp usr;
  char       *line;
  FILE       *fp;

  memset(&usr, 0, sizeof(usr));
  usr.ut_type = USER_PROCESS;
  usr.ut_pid  = getpid();
  line        = ttyname(STDIN_FILENO);
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

  /* wtmp: an ever-growing append-only log of the same record, just
 * opened differently */
  fp = fopen(WTMP_PATH, "a");
  if (fp) {
    fwrite(&usr, sizeof(usr), 1, fp);
    fclose(fp);
  }
}
#endif
