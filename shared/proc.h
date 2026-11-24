/* see LICENSE file for copyright and license details */
#ifndef ARUU_PROC_H
#define ARUU_PROC_H

#include <sys/types.h>

#include <limits.h>
#include <stddef.h>
#include <time.h>

struct procstat {
  int                pid;
  char               comm[PATH_MAX + 2]; /* + 2 for '(' and ')' */
  unsigned char      state;
  int                ppid;
  int                pgrp;
  int                sid;
  int                tty_nr;
  int                tpgid;
  unsigned           flags;
  unsigned long      minflt;
  unsigned long      cminflt;
  unsigned long      majflt;
  unsigned long      cmajflt;
  unsigned long      utime;
  unsigned long      stime;
  long               cutime;
  long               cstime;
  long               priority;
  long               nice;
  long               num_threads;
  long               itrealvalue;
  unsigned long long starttime;
  unsigned long      vsize;
  long               rss;
  unsigned long      rsslim;
};

struct procstatus {
  uid_t uid;
  uid_t euid;
  gid_t gid;
  gid_t egid;
};

int parsecmdline(pid_t pid, char *buf, size_t siz);
int parsestat(pid_t pid, struct procstat *ps);
int parsestatus(pid_t pid, struct procstatus *pstatus);
int proceuid(pid_t pid, uid_t *euid);
int procuid(pid_t pid, uid_t *euid);
int pidfile(const char *file);

/* the process table, as every kernel's tools want it: one row per process in
 * words that are not any kernel's. proc_linux.c reads it from /proc, and
 * proc_posix.c says a kernel has no backend yet, so ps, pgrep and pkill
 * are the same code everywhere and a port is one file */
struct procinfo {
  pid_t              pid, ppid, pgid, sid;
  uid_t              ruid, euid;
  gid_t              rgid, egid;
  unsigned           tty_major, tty_minor; /* 0 0 for no terminal */
  char               state;                /* R S D Z T, as ps prints it */
  long               nice, priority;
  long               nthreads;
  time_t             start;    /* when it began, seconds since the epoch */
  unsigned long      cpu;      /* seconds of cpu time, system and user */
  unsigned long      vsize_kib;
  long               rss_kib;
  char               comm[256];
};

typedef int proc_fn(const struct procinfo *p, void *arg);

/* fn for every process there is, until it returns non zero. -1 with errno
 * set when the table cannot be read, ENOTSUP where there is no backend */
int proc_each(proc_fn *fn, void *arg);
int proc_info(pid_t pid, struct procinfo *p);
/* the command line, its words joined by spaces. -1 for a process with none,
 * a kernel thread or a zombie */
int proc_args(pid_t pid, char *buf, size_t n);
/* the working directory of a process, -1 when it cannot be told: it is gone,
 * it is another user's, or the kernel keeps no such thing */
int proc_cwd(pid_t pid, char *buf, size_t n);
/* the terminal's name as ps shows it: pts/3, tty1 or ? for none */
void proc_ttyname(unsigned major, unsigned minor, char *buf, size_t n);
/* the path of the running program's own file, readlink style: not
 * terminated, -1 where the kernel does not tell */
ssize_t proc_selfexe(char *buf, size_t n);

#endif
