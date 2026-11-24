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

static void
entry(struct SessionEntry *e, const struct utmp *u)
{
  memset(e, 0, sizeof(*e));
  memcpy(e->user, u->ut_name, MIN(sizeof(e->user) - 1, sizeof(u->ut_name)));
  memcpy(e->line, u->ut_line, MIN(sizeof(e->line) - 1, sizeof(u->ut_line)));
  memcpy(e->host, u->ut_host, MIN(sizeof(e->host) - 1, sizeof(u->ut_host)));
  e->time  = u->ut_time;
  e->login = u->ut_type == USER_PROCESS;
}

int
session_each(const char *path, session_fn *fn, void *arg)
{
  struct SessionEntry e;
  struct utmp         u;
  FILE               *fp;
  int                 r = 0;

  if (!(fp = fopen(path, "r")))
    return -1;
  while (fread(&u, sizeof(u), 1, fp) == 1) {
    entry(&e, &u);
    if (fn(&e, arg))
      break;
  }
  if (ferror(fp))
    r = -1;
  if (fclose(fp))
    r = -1;
  return r;
}

void
session_clear(const char *line)
{
  struct utmp usr;
  char        ut_line_buf[UT_LINESIZE + 1];
  FILE       *fp;
  long        pos;

  fp = fopen(UTMP_PATH, "r+");
  if (!fp)
    return;
  do {
    pos = ftell(fp);
    if (fread(&usr, sizeof(usr), 1, fp) != 1)
      break;
    if (usr.ut_line[0] == '\0')
      continue;
    memcpy(ut_line_buf, usr.ut_line, UT_LINESIZE);
    ut_line_buf[UT_LINESIZE] = '\0';
    if (strcmp(ut_line_buf, line) != 0)
      continue;
    memset(&usr, 0, sizeof(usr));
    fseek(fp, pos, SEEK_SET);
    if (fwrite(&usr, sizeof(usr), 1, fp) != 1)
      break;
  } while (1);
  if (ferror(fp))
    weprintf("%s: I/O error:", UTMP_PATH);
  fclose(fp);
}

int
session_lastlog(FILE *fp, uid_t uid, struct SessionEntry *e)
{
  struct lastlog ll;

  memset(&ll, 0, sizeof(ll));
  fseek(fp, uid * sizeof(struct lastlog), 0);
  fread(&ll, sizeof(struct lastlog), 1, fp);
  if (ferror(fp))
    return -1;
  memset(e, 0, sizeof(*e));
  memcpy(e->line, ll.ll_line, MIN(sizeof(e->line) - 1, sizeof(ll.ll_line)));
  memcpy(e->host, ll.ll_host, MIN(sizeof(e->host) - 1, sizeof(ll.ll_host)));
  /* on glibc ll_time can be an int32_t with compat32 */
  e->time = ll.ll_time;
  return 0;
}
#endif
