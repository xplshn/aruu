/* see LICENSE file for copyright and license details */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utmpx.h>

#include "config.h"
#include "util.h"

int get_uptime(long *);
int get_loads(double *);

static void
usage(void)
{
  eprintf("usage: %s\n", argv0);
}

// ?man uptime: show system uptime
// ?man display how long the system has been running and load averages
int
main(int argc, char *argv[])
{
  struct utmpx utx;
  FILE        *ufp;
  long         uptime;
  double       loads[3];
  time_t       tmptime;
  struct tm   *now;
  unsigned int days, hours, minutes;
  int          nusers = 0;
  size_t       n;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND;

  if (argc)
    usage();

  if (get_uptime(&uptime) < 0)
    eprintf("get_uptime:");
  if (get_loads(loads) < 0)
    eprintf("get_loads:");

  time(&tmptime);
  now = localtime(&tmptime);
  printf(" %02d:%02d:%02d up ", now->tm_hour, now->tm_min, now->tm_sec);

  uptime /= 60;
  minutes = uptime % 60;
  uptime /= 60;
  hours = uptime % 24;
  days  = uptime / 24;
  if (days)
    printf("%d day%s, ", days, days != 1 ? "s" : "");
  if (hours)
    printf("%2d:%02d, ", hours, minutes);
  else
    printf("%d min, ", minutes);

  if ((ufp = fopen(UTMP_PATH, "r"))) {
    while ((n = fread(&utx, sizeof(utx), 1, ufp)) > 0) {
      if (!utx.ut_user[0])
        continue;
      if (utx.ut_type != USER_PROCESS)
        continue;
      nusers++;
    }
    if (ferror(ufp))
      eprintf("%s: read error:", UTMP_PATH);
    fclose(ufp);
    printf(" %d user%s, ", nusers, nusers != 1 ? "s" : "");
  }

  printf(" load average: %.02f, %.02f, %.02f\n", loads[0], loads[1], loads[2]);

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    return 1;

  return 0;
}
