/* see LICENSE file for copyright and license details */
#include <sys/ioctl.h>
#include <sys/types.h>

#include <ctype.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "proc.h"
#include "util.h"

/* what -o can name, and what each is headed with. a column says how to write
 * one process into text, so the default, -f, -l and -o all print the same
 * way */
struct column {
  const char *name, *head;
  int         width; /* negative: left aligned */
  void (*put)(const struct procinfo *p, char *out, size_t n);
};

enum { PS_A = 1 << 0, PS_a = 1 << 1, PS_d = 1 << 2, PS_f = 1 << 3, PS_l = 1 << 4 };

static int flags;

/* the lists -p -t -u -U -g -G take: comma or space separated words */
struct list {
  char **w;
  int    n;
};

static struct list pids, ttys, users, ruser, sids, rgroups;
static char       *headers[32];
static const struct column *chosen[32];
static int         nchosen;
static int         allheadless; /* every -o column had its own header text, empty */

static void
username(uid_t uid, char *out, size_t n)
{
  struct passwd *pw = getpwuid(uid);

  if (pw)
    snprintf(out, n, "%s", pw->pw_name);
  else
    snprintf(out, n, "%u", (unsigned)uid);
}

static void
groupname(gid_t gid, char *out, size_t n)
{
  struct group *gr = getgrgid(gid);

  if (gr)
    snprintf(out, n, "%s", gr->gr_name);
  else
    snprintf(out, n, "%u", (unsigned)gid);
}

static void
hms(unsigned long secs, char *out, size_t n)
{
  snprintf(out, n, "%02lu:%02lu:%02lu", secs / 3600, (secs % 3600) / 60, secs % 60);
}

static void put_user(const struct procinfo *p, char *o, size_t n)   { username(p->euid, o, n); }
static void put_ruser(const struct procinfo *p, char *o, size_t n)  { username(p->ruid, o, n); }
static void put_group(const struct procinfo *p, char *o, size_t n)  { groupname(p->egid, o, n); }
static void put_rgroup(const struct procinfo *p, char *o, size_t n) { groupname(p->rgid, o, n); }
static void put_pid(const struct procinfo *p, char *o, size_t n)    { snprintf(o, n, "%d", (int)p->pid); }
static void put_ppid(const struct procinfo *p, char *o, size_t n)   { snprintf(o, n, "%d", (int)p->ppid); }
static void put_pgid(const struct procinfo *p, char *o, size_t n)   { snprintf(o, n, "%d", (int)p->pgid); }
static void put_nice(const struct procinfo *p, char *o, size_t n)   { snprintf(o, n, "%ld", p->nice); }
static void put_pri(const struct procinfo *p, char *o, size_t n)    { snprintf(o, n, "%ld", p->priority); }
static void put_vsz(const struct procinfo *p, char *o, size_t n)    { snprintf(o, n, "%lu", p->vsize_kib); }
static void put_sz(const struct procinfo *p, char *o, size_t n)     { snprintf(o, n, "%lu", p->vsize_kib / 4); }
static void put_state(const struct procinfo *p, char *o, size_t n)  { snprintf(o, n, "%c", p->state); }
static void put_time(const struct procinfo *p, char *o, size_t n)   { hms(p->cpu, o, n); }
static void put_comm(const struct procinfo *p, char *o, size_t n)   { snprintf(o, n, "%s", p->comm); }
static void put_zero(const struct procinfo *p, char *o, size_t n)   { (void)p; snprintf(o, n, "0"); }
static void put_dash(const struct procinfo *p, char *o, size_t n)   { (void)p; snprintf(o, n, "-"); }

static void
put_tty(const struct procinfo *p, char *o, size_t n)
{
  proc_ttyname(p->tty_major, p->tty_minor, o, n);
}

/* the command line, or the name in brackets for what has none, as a kernel
 * thread */
static void
put_args(const struct procinfo *p, char *o, size_t n)
{
  char args[BUFSIZ];

  size_t len;

  if (proc_args(p->pid, args, sizeof args) < 0) {
    snprintf(o, n, "[%s]", p->comm);
    return;
  }
  /* the words end with the nul that ended the last, made a space */
  for (len = strlen(args); len && args[len - 1] == ' '; len--)
    args[len - 1] = '\0';
  snprintf(o, n, "%s", args);
}

/* the program's name, and with -f its command line: what CMD says */
static void
put_cmd(const struct procinfo *p, char *o, size_t n)
{
  if (flags & PS_f)
    put_args(p, o, n);
  else
    put_comm(p, o, n);
}

/* how long it has run: [[dd-]hh:]mm:ss */
static void
put_etime(const struct procinfo *p, char *o, size_t n)
{
  unsigned long s = time(NULL) > p->start ? (unsigned long)(time(NULL) - p->start) : 0;

  if (s >= 86400)
    snprintf(o, n, "%lu-%02lu:%02lu:%02lu", s / 86400, (s % 86400) / 3600, (s % 3600) / 60, s % 60);
  else if (s >= 3600)
    snprintf(o, n, "%02lu:%02lu:%02lu", s / 3600, (s % 3600) / 60, s % 60);
  else
    snprintf(o, n, "%02lu:%02lu", s / 60, s % 60);
}

/* when it began: the time of day, or the day when it was before today */
static void
put_stime(const struct procinfo *p, char *o, size_t n)
{
  struct tm tm, now;
  time_t    t = time(NULL);

  localtime_r(&p->start, &tm);
  localtime_r(&t, &now);
  if (tm.tm_yday == now.tm_yday && tm.tm_year == now.tm_year)
    strftime(o, n, "%H:%M", &tm);
  else
    strftime(o, n, "%b%d", &tm);
}

/* the share of the time it has been there that it spent running */
static void
put_pcpu(const struct procinfo *p, char *o, size_t n)
{
  time_t life = time(NULL) - p->start;

  snprintf(o, n, "%.1f", life > 0 ? 100.0 * (double)p->cpu / (double)life : 0.0);
}

static const struct column columns[] = {
    {"user", "USER", -8, put_user},     {"ruser", "RUSER", -8, put_ruser},
    {"group", "GROUP", -8, put_group},  {"rgroup", "RGROUP", -8, put_rgroup},
    {"uid", "UID", -8, put_user},       {"pid", "PID", 5, put_pid},
    {"ppid", "PPID", 5, put_ppid},      {"pgid", "PGID", 5, put_pgid},
    {"etime", "ELAPSED", 11, put_etime},{"nice", "NI", 3, put_nice},
    {"pcpu", "%CPU", 4, put_pcpu},      {"vsz", "VSZ", 7, put_vsz},
    {"time", "TIME", 8, put_time},      {"tty", "TT", -8, put_tty},
    {"comm", "COMMAND", -15, put_comm}, {"args", "COMMAND", -15, put_args},
    /* the names of -f and -l, which are not the ones of -o */
    {"f", "F", 1, put_zero},            {"s", "S", 1, put_state},
    {"c", "C", 2, put_zero},            {"pri", "PRI", 3, put_pri},
    {"addr", "ADDR", 4, put_dash},      {"sz", "SZ", 6, put_sz},
    {"wchan", "WCHAN", 6, put_dash},    {"stime", "STIME", 5, put_stime},
    {"cmd", "CMD", -15, put_cmd},
};

static const struct column *
column(const char *name)
{
  size_t i;

  for (i = 0; i < sizeof columns / sizeof *columns; i++)
    if (!strcmp(columns[i].name, name))
      return &columns[i];
  return NULL;
}

static void
choose(const char *name, const char *head)
{
  const struct column *c = column(name);

  if (!c)
    eprintf("%s: unknown format keyword\n", name);
  if (nchosen == 32)
    eprintf("too many columns\n");
  headers[nchosen]  = (char *)(head ? head : c->head);
  chosen[nchosen++] = c;
}

/* -o's list: keywords, each with its own header after an =. a list whose
 * every header is empty prints no header line */
static void
add_format(char *arg)
{
  char *tok, *eq;

  for (tok = strtok(arg, ", "); tok; tok = strtok(NULL, ", ")) {
    eq = strchr(tok, '=');
    if (eq) {
      *eq = '\0';
      choose(tok, eq + 1);
      if (*(eq + 1) == '\0')
        allheadless++;
    } else {
      choose(tok, NULL);
    }
  }
}

static void
add_list(struct list *l, char *arg)
{
  char *tok;

  for (tok = strtok(arg, ", "); tok; tok = strtok(NULL, ", ")) {
    l->w = ereallocarray(l->w, l->n + 1, sizeof(*l->w));
    l->w[l->n++] = tok;
  }
}

static int
in_list(const struct list *l, const char *s)
{
  int i;

  for (i = 0; i < l->n; i++)
    if (!strcmp(l->w[i], s))
      return 1;
  return 0;
}

static int
user_in(const struct list *l, uid_t uid)
{
  char name[64], num[16];

  username(uid, name, sizeof name);
  snprintf(num, sizeof num, "%u", (unsigned)uid);
  return in_list(l, name) || in_list(l, num);
}

static int
group_in(const struct list *l, gid_t gid)
{
  char name[64], num[16];

  groupname(gid, name, sizeof name);
  snprintf(num, sizeof num, "%u", (unsigned)gid);
  return in_list(l, name) || in_list(l, num);
}

/* a terminal as -t names it: pts/3, or tty1, or the same without the dir */
static int
tty_in(const struct list *l, const struct procinfo *p)
{
  char name[32];

  proc_ttyname(p->tty_major, p->tty_minor, name, sizeof name);
  return in_list(l, name) || (!strncmp(name, "pts/", 4) && in_list(l, name + 4));
}

static char mytty[32];
static uid_t myuid;
static int    have_tty;

/* whether a process is one of those asked for */
static int
wanted(const struct procinfo *p)
{
  char num[16];
  int  selected = 0, listed = 0;

  snprintf(num, sizeof num, "%d", (int)p->pid);
  if (pids.n) { listed = 1; selected |= in_list(&pids, num); }
  if (ttys.n) { listed = 1; selected |= tty_in(&ttys, p); }
  if (users.n) { listed = 1; selected |= user_in(&users, p->euid); }
  if (ruser.n) { listed = 1; selected |= user_in(&ruser, p->ruid); }
  if (rgroups.n) { listed = 1; selected |= group_in(&rgroups, p->rgid); }
  if (sids.n) { listed = 1; selected |= in_list(&sids, (snprintf(num, sizeof num, "%d", (int)p->sid), num)); }
  if (flags & PS_A)
    return 1;
  if (flags & PS_a) {
    if (p->pid != p->sid && (p->tty_major || p->tty_minor))
      return 1;
    return selected;
  }
  if (flags & PS_d) {
    if (p->pid != p->sid)
      return 1;
    return selected;
  }
  if (listed)
    return selected;
  /* nothing asked for: the invoker's, on the invoker's terminal */
  if (p->euid != myuid)
    return 0;
  if (have_tty) {
    char name[32];

    proc_ttyname(p->tty_major, p->tty_minor, name, sizeof name);
    return !strcmp(name, mytty);
  }
  return 1;
}

static int width;

static int
show(const struct procinfo *p, void *arg)
{
  char line[BUFSIZ * 2], cell[BUFSIZ];
  size_t at = 0;
  int  i;

  (void)arg;
  if (!wanted(p))
    return 0;
  for (i = 0; i < nchosen; i++) {
    chosen[i]->put(p, cell, sizeof cell);
    /* the last column runs as long as it is */
    if (i == nchosen - 1)
      at += (size_t)snprintf(line + at, sizeof line - at, "%s%s", i ? " " : "", cell);
    else
      at += (size_t)snprintf(line + at, sizeof line - at, "%s%*s", i ? " " : "", chosen[i]->width, cell);
    if (at >= sizeof line)
      at = sizeof line - 1;
  }
  if (width && at > (size_t)width)
    line[width] = '\0';
  puts(line);
  return 0;
}

static void
usage(void)
{
  eprintf(
      "usage: %s [-aAdefl] [-g grouplist] [-G grouplist] [-o format]... [-p proclist]\n"
      "          [-t termlist] [-u userlist] [-U userlist]\n",
      argv0
  );
}

// ?man ps: report process status
// ?man ps writes information about processes: by default those of the
// ?man invoker on the terminal it was run from. the processes of every
// ?man kernel it has a process table backend for are written the same
int
main(int argc, char *argv[])
{
  char *name;
  int   i;
  struct winsize w = {0};

  ARGBEGIN
  {
    // ?man -A: every process
    case 'A':
      flags |= PS_A;
      break;
    // ?man -e: every process, as -A
    case 'e':
      flags |= PS_A;
      break;
    // ?man -a: the processes with a terminal that are not session leaders
    case 'a':
      flags |= PS_a;
      break;
    // ?man -d: every process but the session leaders
    case 'd':
      flags |= PS_d;
      break;
    // ?man -f: a full listing, with the owner, the parent and the start
    case 'f':
      flags |= PS_f;
      break;
    // ?man -l: a long listing
    case 'l':
      flags |= PS_l;
      break;
    // ?man -n: accepted and ignored, the name list of the system is not used
    case 'n':
      (void)EARGF(usage());
      break;
    // ?man -o:format: the columns written, keywords with an optional =header
    case 'o':
      add_format(EARGF(usage()));
      break;
    // ?man -p:proclist: only these processes
    case 'p':
      add_list(&pids, EARGF(usage()));
      break;
    // ?man -t:termlist: only the processes on these terminals
    case 't':
      add_list(&ttys, EARGF(usage()));
      break;
    // ?man -u:userlist: only the processes of these effective users
    case 'u':
      add_list(&users, EARGF(usage()));
      break;
    // ?man -U:userlist: only the processes of these real users
    case 'U':
      add_list(&ruser, EARGF(usage()));
      break;
    // ?man -g:sidlist: only the processes of these sessions
    case 'g':
      add_list(&sids, EARGF(usage()));
      break;
    // ?man -G:grouplist: only the processes of these real groups
    case 'G':
      add_list(&rgroups, EARGF(usage()));
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc)
    usage();

  /* the default, -f and -l are named lists of columns */
  if (!nchosen) {
    const char *set[] = {"pid", "tty", "time", "cmd"};
    const char *full[] = {"uid", "pid", "ppid", "c", "stime", "tty", "time", "cmd"};
    const char *lng[] = {"f", "s", "uid", "pid", "ppid", "c", "pri", "nice", "addr", "sz", "wchan", "tty", "time", "cmd"};
    const char **l = (flags & PS_l) ? lng : (flags & PS_f) ? full : set;
    size_t       k, n = (flags & PS_l) ? sizeof lng / sizeof *lng : (flags & PS_f) ? sizeof full / sizeof *full : sizeof set / sizeof *set;

    for (k = 0; k < n; k++)
      choose(l[k], NULL);
  }

  if ((name = ttyname(0))) {
    unsigned maj = 0, min = 0;
    struct procinfo me;

    if (proc_info(getpid(), &me) == 0) {
      maj = me.tty_major;
      min = me.tty_minor;
    }
    proc_ttyname(maj, min, mytty, sizeof mytty);
    have_tty = mytty[0] != '?' || mytty[1];
    (void)name;
  }
  myuid = geteuid();
  if (ioctl(1, TIOCGWINSZ, &w) == 0)
    width = w.ws_col;

  /* a header line, unless every column was given an empty header */
  if (allheadless < nchosen) {
    char head[BUFSIZ];
    size_t at = 0;

    for (i = 0; i < nchosen; i++) {
      if (i == nchosen - 1)
        at += (size_t)snprintf(head + at, sizeof head - at, "%s%s", i ? " " : "", headers[i]);
      else
        at += (size_t)snprintf(head + at, sizeof head - at, "%s%*s", i ? " " : "", chosen[i]->width, headers[i]);
      if (at >= sizeof head)
        at = sizeof head - 1;
    }
    puts(head);
  }
  if (proc_each(show, NULL) < 0)
    eprintf("the process table cannot be read:");
  return fshut(stdout, "<stdout>");
}
