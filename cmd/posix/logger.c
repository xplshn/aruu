/* see LICENSE file for copyright and license details */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#define SYSLOG_NAMES
#include <syslog.h>
#include <unistd.h>

#include "util.h"

static int
decodetable(CODE *table, char *name)
{
  CODE *c;

  for (c = table; c->c_name; c++)
    if (!strcasecmp(name, c->c_name))
      return c->c_val;
  eprintf("invalid priority name: %s\n", name);

  return -1; /* not reached */
}

static int
decodepri(char *pri)
{
  char *lev, *fac = pri;

  if (!(lev = strchr(pri, '.')))
    eprintf("invalid priority name: %s\n", pri);
  *lev++ = '\0';
  if (!*lev)
    eprintf("invalid priority name: %s\n", pri);

  return (decodetable(facilitynames, fac) & LOG_FACMASK)
         | (decodetable(prioritynames, lev) & LOG_PRIMASK);
}

static void
usage(void)
{
  eprintf("usage: %s [-is] [-p priority] [-t tag] [message ...]\n", argv0);
}

// ?man logger: log messages
// ?man arguments: message ...
// ?man add messages to the system log
int
main(int argc, char *argv[])
{
  size_t sz;
  int    logflags = 0, priority = LOG_NOTICE, i;
  char  *buf = NULL, *tag = NULL;

  ARGBEGIN
  {
    // ?man -i: interactive mode or prompt for confirmation
    case 'i':
      logflags |= LOG_PID;
      break;
    // ?man -p:str: preserve file attributes
    case 'p':
      priority = decodepri(EARGF(usage()));
      break;
    // ?man -s: silent mode or print summary
    case 's':
      logflags |= LOG_PERROR;
      break;
    // ?man -t:str: sort or specify timestamp
    case 't':
      tag = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND

  openlog(tag ? tag : getlogin(), logflags, 0);

  if (!argc) {
    while (getline(&buf, &sz, stdin) > 0)
      syslog(priority, "%s", buf);
  } else {
    for (i = 0, sz = 0; i < argc; i++)
      sz += strlen(argv[i]);
    sz += argc;
    buf = ecalloc(1, sz);
    for (i = 0; i < argc; i++) {
      estrlcat(buf, argv[i], sz);
      if (i + 1 < argc)
        estrlcat(buf, " ", sz);
    }
    syslog(priority, "%s", buf);
  }

  closelog();

  return fshut(stdin, "<stdin>");
}
