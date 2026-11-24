/* see LICENSE file for copyright and license details */

#include <sys/types.h>

#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"
#include "queue.h"
#include "util.h"

struct pidentry {
  pid_t pid;
  SLIST_ENTRY(pidentry) entry;
};

static SLIST_HEAD(, pidentry) omitpid_head;

static void
usage(void)
{
  eprintf("usage: %s [-o pid1,pid2,...pidN] [-s] [program...]\n", argv0);
}

/* what main sets for the walk of the process table */
struct pidof {
  int    argc;
  char **argv;
  int    sflag, oflag, found;
};

static int
each(const struct procinfo *p, void *arg)
{
  struct pidof    *w = arg;
  struct pidentry *pe;
  char             cmdline[BUFSIZ], *cmd, *cmdbase, *sp;
  char             word[16];
  int              i;

  if (w->oflag) {
    SLIST_FOREACH(pe, &omitpid_head, entry)
    if (pe->pid == p->pid)
      return 0;
  }
  if (proc_args(p->pid, cmdline, sizeof(cmdline)) < 0) {
    cmd     = (char *)p->comm;
    cmdbase = cmd;
  } else {
    if ((sp = strchr(cmdline, ' ')))
      *sp = '\0';
    cmd     = cmdline;
    cmdbase = basename(cmdline);
  }
  /* workaround for login shells */
  if (cmd[0] == '-')
    cmd++;
  snprintf(word, sizeof(word), "%d", (int)p->pid);
  for (i = 0; i < w->argc; i++) {
    if (strcmp(cmd, w->argv[i]) == 0 || strcmp(cmdbase, w->argv[i]) == 0) {
      putword(stdout, word);
      w->found++;
      if (w->sflag)
        return 1;
    }
  }
  return 0;
}

// ?man pidof: find process ids
// ?man arguments: -o pid1
// ?man find the process identity numbers of running programs
int
main(int argc, char *argv[])
{
  char            *p, *arg = NULL;
  struct pidof     w = {0};
  struct pidentry *pe;

  ARGBEGIN
  {
    // ?man -s: silent mode or print summary
    case 's':
      w.sflag = 1;
      break;
    // ?man -o:str: specify output file
    case 'o':
      w.oflag = 1;
      arg     = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND;

  if (!argc)
    return 1;
  w.argc = argc;
  w.argv = argv;

  SLIST_INIT(&omitpid_head);

  if (w.oflag) {
    for (p = strtok(arg, ","); p; p = strtok(NULL, ",")) {
      pe = emalloc(sizeof(*pe));
      if (strcmp(p, "%PPID") == 0)
        pe->pid = getppid();
      else
        pe->pid = estrtol(p, 10);
      SLIST_INSERT_HEAD(&omitpid_head, pe, entry);
    }
  }

  if (proc_each(each, &w) < 0)
    eprintf("the process table cannot be read:");
  if (w.found)
    putchar('\n');

  return w.found ? 0 : 1;
}
