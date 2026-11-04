/* see LICENSE file for copyright and license details */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "wexec.h"

static void
usage(void)
{
  eprintf("usage: %s [-t] [-n interval] command\n", argv0);
}

// ?man watch: execute periodically
// ?man arguments: command
// ?man run a program periodically, showing output fullscreen
int
main(int argc, char *argv[])
{
  char            cmd[BUFSIZ];
  char           *end;
  struct timespec interval = { .tv_sec = 2, .tv_nsec = 0 };
  float           period;
  int        i;

  ARGBEGIN
  {
    // ?man -t: sort or specify timestamp
    case 't':
      break;
    // ?man -n:str: print line numbers or counts
    case 'n':
      period = strtof(EARGF(usage()), &end);
      if (*end != '\0' || errno != 0)
        eprintf("invalid interval\n");
      if (period < 0)
        period = 0.1f;
      interval.tv_sec = period;
      interval.tv_nsec = (period - interval.tv_sec) * 1E9;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc < 1)
    usage();

  if (strlcpy(cmd, argv[0], sizeof(cmd)) >= sizeof(cmd))
    eprintf("command too long\n");
  for (i = 1; i < argc; i++) {
    if (strlcat(cmd, " ", sizeof(cmd)) >= sizeof(cmd))
      eprintf("command too long\n");
    if (strlcat(cmd, argv[i], sizeof(cmd)) >= sizeof(cmd))
      eprintf("command too long\n");
  }

  for (;;) {
    printf("\x1b[2J\x1b[H"); /* clear */
    fflush(NULL);
    wsystem(cmd);
    nanosleep(&interval, NULL);
  }
  return 0;
}
