/* see LICENSE file for copyright and license details */

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "paths.h"
#include "session.h"
#include "text.h"
#include "util.h"

#define PASSWD "/etc/passwd"

static FILE *last;

static void
lastlog(char *user)
{
  struct passwd      *pwd;
  struct SessionEntry e;

  errno = 0;
  if ((pwd = getpwnam(user)) == NULL) {
    if (errno)
      weprintf("getpwnam %s:", user);
    else
      weprintf("unknown user: %s\n", user);
    return;
  }

  if (session_lastlog(last, pwd->pw_uid, &e) < 0)
    eprintf("%s: read error:", ARUU_PATH_LASTLOG);
  printf("%-8.8s %-8.8s %-16.16s %s", user, e.line, e.host, ctime(&e.time));
}

// ?man lastlog: report recent logins
// ?man display the most recent login times of users
int
main(int argc, char **argv)
{
  FILE  *fp;
  char  *line = NULL, *p;
  size_t sz   = 0;

  if ((last = fopen(ARUU_PATH_LASTLOG, "r")) == NULL)
    eprintf("fopen %s:", ARUU_PATH_LASTLOG);

  if (argc > 1) {
    while (*++argv)
      lastlog(*argv);
  } else {
    if ((fp = fopen(PASSWD, "r")) == NULL)
      eprintf("fopen %s:", PASSWD);
    while (agetline(&line, &sz, fp) != -1) {
      if ((p = strchr(line, ':')) == NULL)
        eprintf("invalid passwd entry\n");
      *p = '\0';
      lastlog(line);
    }
    if (fclose(fp))
      eprintf("fclose %s:", PASSWD);
    free(line);
  }

  if (fclose(last))
    eprintf("fclose %s:", ARUU_PATH_LASTLOG);

  return 0;
}
