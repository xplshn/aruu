/* see LICENSE file for copyright and license details */

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "proc.h"
#include "queue.h"
#include "util.h"

struct {
  const char *name;
  int         sig;
} sigs[] = {
#define SIG(n) {#n, SIG##n}
    SIG(ABRT), SIG(ALRM), SIG(BUS),  SIG(CHLD), SIG(CONT), SIG(FPE),  SIG(HUP),
    SIG(ILL),  SIG(INT),  SIG(KILL), SIG(PIPE), SIG(QUIT), SIG(SEGV), SIG(STOP),
    SIG(TERM), SIG(TSTP), SIG(TTIN), SIG(TTOU), SIG(USR1), SIG(USR2), SIG(URG),
#undef SIG
};

struct pidentry {
  pid_t pid;
  SLIST_ENTRY(pidentry) entry;
};

static SLIST_HEAD(, pidentry) omitpid_head;

static void
usage(void)
{
  eprintf("usage: %s [-o pid1,pid2,..,pidN] [-s signal]\n", argv0);
}

struct killall {
  int sig, oflag;
};

static int
each(const struct procinfo *p, void *arg)
{
  struct killall  *w = arg;
  struct pidentry *pe;

  if (p->pid == 1 || p->pid == getpid() || getsid(p->pid) == getsid(0) || getsid(p->pid) == 0)
    return 0;
  if (w->oflag) {
    SLIST_FOREACH(pe, &omitpid_head, entry)
    if (pe->pid == p->pid)
      return 0;
  }
  kill(p->pid, w->sig);
  return 0;
}

// ?man killall5: send signal to all processes
// ?man arguments: -o pid1
// ?man send a signal to all processes except kernel threads
int
main(int argc, char *argv[])
{
  struct pidentry *pe;
  struct killall   w = {SIGTERM, 0};
  char            *p, *arg = NULL;
  char            *end, *v;
  size_t           i;

  ARGBEGIN
  {
    // ?man -s:str: silent mode or print summary
    case 's':
      v     = EARGF(usage());
      w.sig = strtol(v, &end, 0);
      if (*end == '\0')
        break;
      for (i = 0; i < LEN(sigs); i++) {
        if (strcasecmp(v, sigs[i].name) == 0) {
          w.sig = sigs[i].sig;
          break;
        }
      }
      if (i == LEN(sigs))
        eprintf("%s: unknown signal\n", v);
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

  SLIST_INIT(&omitpid_head);

  if (w.oflag) {
    for (p = strtok(arg, ","); p; p = strtok(NULL, ",")) {
      pe      = emalloc(sizeof(*pe));
      pe->pid = estrtol(p, 10);
      SLIST_INSERT_HEAD(&omitpid_head, pe, entry);
    }
  }

  if (w.sig != SIGSTOP && w.sig != SIGCONT)
    kill(-1, SIGSTOP);

  if (proc_each(each, &w) < 0)
    eprintf("the process table cannot be read:");

  if (w.sig != SIGSTOP && w.sig != SIGCONT)
    kill(-1, SIGCONT);

  return 0;
}
