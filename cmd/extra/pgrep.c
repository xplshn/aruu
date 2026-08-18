/* see LICENSE file for copyright and license details */
#include <sys/types.h>

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "paths.h"
#include "proc.h"
#include "util.h"

enum {
  PGREP_fflag = 1 << 0, /* match against full cmdline, not just comm */
  PGREP_lflag = 1 << 1, /* list pid and comm */
  PGREP_aflag = 1 << 2, /* list pid and full cmdline */
  PGREP_xflag = 1 << 3, /* require an exact match */
  PGREP_vflag = 1 << 4, /* invert: print non-matching pids */
  PGREP_cflag = 1 << 5, /* print a match count, not the pids */
};

struct hit {
  pid_t              pid;
  unsigned long long starttime;
};

static int         flags;
static regex_t     preg;
static uid_t       user_uid;
static int         have_user;
static pid_t       self;
static struct hit  *hits;
static size_t       nhits, hitscap;
static int          count;

// newest/oldest are mutually exclusive with each other and with
// printing every match as it is found, so they buffer every match
// instead of streaming it: 0 means "print as found"
enum { PGREP_NONE = 0, PGREP_NEWEST, PGREP_OLDEST };
static int select_mode;

static int
matches(const char *target)
{
  regmatch_t m;
  int        r;

  r = regexec(&preg, target, 1, &m, 0);
  if (r != 0)
    return 0;
  if (flags & PGREP_xflag)
    return m.rm_so == 0 && (size_t)m.rm_eo == strlen(target);
  return 1;
}

static void
record(pid_t pid, unsigned long long starttime)
{
  if (nhits == hitscap) {
    hitscap = hitscap ? hitscap * 2 : 64;
    hits    = ereallocarray(hits, hitscap, sizeof(*hits));
  }
  hits[nhits].pid       = pid;
  hits[nhits].starttime = starttime;
  nhits++;
}

static void
show(pid_t pid, const char *comm)
{
  char cmdline[BUFSIZ];

  count++;
  if (flags & PGREP_cflag)
    return;
  if (flags & PGREP_aflag) {
    if (parsecmdline(pid, cmdline, sizeof(cmdline)) < 0)
      printf("%d %s\n", pid, comm);
    else
      printf("%d %s\n", pid, cmdline);
  } else if (flags & PGREP_lflag) {
    printf("%d %s\n", pid, comm);
  } else {
    printf("%d\n", pid);
  }
}

static void
pgrepr(const char *file)
{
  char              path[PATH_MAX], *p;
  struct procstat   ps;
  struct procstatus pstatus;
  char              cmdline[BUFSIZ];
  const char       *target;
  pid_t             pid;
  int               matched;

  if (strlcpy(path, file, sizeof(path)) >= sizeof(path))
    eprintf("path too long\n");
  p = basename(path);
  if (pidfile(p) == 0)
    return;
  pid = estrtol(p, 10);
  if (pid == self)
    return;
  if (parsestat(pid, &ps) < 0)
    return;

  if (have_user) {
    if (parsestatus(pid, &pstatus) < 0)
      return;
    if (pstatus.euid != user_uid)
      return;
  }

  if (flags & PGREP_fflag) {
    if (parsecmdline(pid, cmdline, sizeof(cmdline)) < 0)
      target = ps.comm;
    else
      target = cmdline;
  } else {
    target = ps.comm;
  }

  matched = matches(target);
  if (flags & PGREP_vflag)
    matched = !matched;
  if (!matched)
    return;

  if (select_mode != PGREP_NONE)
    record(pid, ps.starttime);
  else
    show(pid, ps.comm);
}

static void
usage(void)
{
  eprintf("usage: %s [-flaxvc] [-u user] [-n | -o] pattern\n", argv0);
}

// ?man pgrep: list processes by name
// ?man arguments: pattern
// ?man pgrep matches pattern, a POSIX extended regular expression,
// ?man against each running process's name (or full command line
// ?man with -f) and prints the pid of every match, one per line
int
main(int argc, char *argv[])
{
  char            namebuf[PATH_MAX];
  struct procstat ps;
  struct passwd  *pw;
  const char     *user = NULL;
  size_t          i, pick;

  ARGBEGIN
  {
    // ?man -f: match the full command line instead of just the name
    case 'f':
      flags |= PGREP_fflag;
      break;
    // ?man -l: list the matched name alongside each pid
    case 'l':
      flags |= PGREP_lflag;
      break;
    // ?man -a: list the full command line alongside each pid
    case 'a':
      flags |= PGREP_aflag;
      break;
    // ?man -x: require pattern to match the whole name or cmdline
    case 'x':
      flags |= PGREP_xflag;
      break;
    // ?man -v: invert the match, printing non-matching pids
    case 'v':
      flags |= PGREP_vflag;
      break;
    // ?man -c: print a count of matches instead of their pids
    case 'c':
      flags |= PGREP_cflag;
      break;
    // ?man -u:user only match processes owned by user (name or uid)
    case 'u':
      user = EARGF(usage());
      break;
    // ?man -n{sel}: select only the most recently started match
    case 'n':
      select_mode = PGREP_NEWEST;
      break;
    // ?man -o{sel}: select only the oldest match
    case 'o':
      select_mode = PGREP_OLDEST;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc != 1)
    usage();

  if (user) {
    errno = 0;
    pw    = getpwnam(user);
    if (pw) {
      user_uid = pw->pw_uid;
    } else {
      if (errno)
        eprintf("getpwnam %s:", user);
      user_uid = (uid_t)estrtonum(user, 0, UINT_MAX);
    }
    have_user = 1;
  }

  eregcomp(&preg, argv[0], REG_EXTENDED);

  self = getpid();
  recurse_dir(ARUU_LINUX_PATH_PROC, pgrepr);

  if (select_mode != PGREP_NONE && nhits > 0) {
    pick = 0;
    for (i = 1; i < nhits; i++) {
      if (select_mode == PGREP_NEWEST ? hits[i].starttime > hits[pick].starttime
                                       : hits[i].starttime < hits[pick].starttime)
        pick = i;
    }
    if (parsestat(hits[pick].pid, &ps) == 0)
      strlcpy(namebuf, ps.comm, sizeof(namebuf));
    else
      namebuf[0] = '\0';
    show(hits[pick].pid, namebuf);
  }

  if (flags & PGREP_cflag)
    printf("%d\n", count);

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    return 2;
  return count > 0 ? 0 : 1;
}
