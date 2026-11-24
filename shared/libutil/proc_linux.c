/* see LICENSE file for copyright and license details */
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../paths.h"
#include "../proc.h"
#include "../util.h"

#include <dirent.h>
#include <time.h>

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)

int
parsecmdline(pid_t pid, char *buf, size_t siz)
{
  int     fd;
  char    path[PATH_MAX];
  ssize_t n, i;

  snprintf(path, sizeof(path), ARUU_LINUX_PATH_PROC "/%ld/cmdline", (long)pid);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  n = read(fd, buf, siz > 0 ? siz - 1 : 0);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';
  for (i = 0; i < n; i++)
    if (buf[i] == '\0')
      buf[i] = ' ';
  return 0;
}

int
parsestat(pid_t pid, struct procstat *ps)
{
  char   path[PATH_MAX], line[BUFSIZ], *lp, *rp;
  FILE  *fp;
  size_t len;
  int    n;

  snprintf(path, sizeof(path), ARUU_LINUX_PATH_PROC "/%d/stat", pid);
  if (!(fp = fopen(path, "r")))
    return -1;
  n = fgets(line, sizeof(line), fp) != NULL;
  fclose(fp);
  /* comm is whatever the process named itself, spaces and parens included,
   * so it runs from the first ( to the last ) */
  if (!n || !(lp = strchr(line, '(')) || !(rp = strrchr(line, ')')) || rp < lp) {
    errno = EINVAL;
    return -1;
  }
  len = MIN((size_t)(rp - lp - 1), sizeof(ps->comm) - 1);
  memcpy(ps->comm, lp + 1, len);
  ps->comm[len] = '\0';
  ps->pid       = pid;
  n             = sscanf(
      rp + 1,
      " %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %llu %lu %ld %lu",
      &ps->state,
      &ps->ppid,
      &ps->pgrp,
      &ps->sid,
      &ps->tty_nr,
      &ps->tpgid,
      &ps->flags,
      &ps->minflt,
      &ps->cminflt,
      &ps->majflt,
      &ps->cmajflt,
      &ps->utime,
      &ps->stime,
      &ps->cutime,
      &ps->cstime,
      &ps->priority,
      &ps->nice,
      &ps->num_threads,
      &ps->itrealvalue,
      &ps->starttime,
      &ps->vsize,
      &ps->rss,
      &ps->rsslim
  );
  if (n != 23) {
    errno = EINVAL;
    return -1;
  }
  return 0;
}

int
parsestatus(pid_t pid, struct procstatus *pstatus)
{
  char    path[PATH_MAX];
  char    buf[BUFSIZ], *off;
  int     fd;
  ssize_t n;

  snprintf(path, sizeof(path), ARUU_LINUX_PATH_PROC "/%d/status", pid);
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return -1;
  buf[n] = '\0';
  off = strstr(buf, "Uid:");
  if (!off)
    return -1;
  sscanf(off, "Uid: %u %u", &pstatus->uid, &pstatus->euid);
  off = strstr(buf, "Gid:");
  if (!off)
    return -1;
  sscanf(off, "Gid: %u %u", &pstatus->gid, &pstatus->egid);
  return 0;
}

int
pidfile(const char *file)
{
  char *end;

  errno = 0;
  strtol(file, &end, 10);
  if (*end != '\0')
    return 0;
  if (errno != 0)
    return 0;
  return 1;
}

/* the terminal of a process as stat gives it: a device number in the
 * kernel's own encoding */
static void
tty_split(int nr, unsigned *major, unsigned *minor)
{
  unsigned dev = (unsigned)nr;

  *major = (dev & 0xfff00) >> 8;
  *minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
}

/* when the system started, seconds since the epoch, from stat's btime */
static time_t
boot_time(void)
{
  static time_t boot;
  char          line[256];
  FILE         *fp;

  if (boot)
    return boot;
  if ((fp = fopen(ARUU_LINUX_PATH_PROC "/stat", "r"))) {
    while (fgets(line, sizeof line, fp)) {
      long long t;

      if (sscanf(line, "btime %lld", &t) == 1) {
        boot = (time_t)t;
        break;
      }
    }
    fclose(fp);
  }
  return boot;
}

int
proc_info(pid_t pid, struct procinfo *p)
{
  struct procstat   ps;
  struct procstatus st;
  long              hz = sysconf(_SC_CLK_TCK);
  long              page = sysconf(_SC_PAGESIZE);

  if (parsestat(pid, &ps) < 0)
    return -1;
  memset(&st, 0, sizeof st);
  parsestatus(pid, &st);
  if (hz <= 0)
    hz = 100;
  memset(p, 0, sizeof *p);
  p->pid       = pid;
  p->ppid      = ps.ppid;
  p->pgid      = ps.pgrp;
  p->sid       = ps.sid;
  p->ruid      = st.uid;
  p->euid      = st.euid;
  p->rgid      = st.gid;
  p->egid      = st.egid;
  p->state     = (char)ps.state;
  p->nice      = ps.nice;
  p->priority  = ps.priority;
  p->nthreads  = ps.num_threads;
  p->start     = boot_time() + (time_t)(ps.starttime / (unsigned long long)hz);
  p->cpu       = (ps.utime + ps.stime) / (unsigned long)hz;
  p->vsize_kib = ps.vsize / 1024;
  p->rss_kib   = ps.rss * (page > 0 ? page / 1024 : 4);
  if (ps.tty_nr)
    tty_split(ps.tty_nr, &p->tty_major, &p->tty_minor);
  strlcpy(p->comm, ps.comm, sizeof p->comm);
  return 0;
}

int
proc_each(proc_fn *fn, void *arg)
{
  struct dirent *e;
  struct procinfo p;
  DIR           *d = opendir(ARUU_LINUX_PATH_PROC);

  if (!d)
    return -1;
  while ((e = readdir(d))) {
    if (!pidfile(e->d_name) || proc_info((pid_t)strtol(e->d_name, NULL, 10), &p) < 0)
      continue;
    if (fn(&p, arg))
      break;
  }
  closedir(d);
  return 0;
}

int
proc_args(pid_t pid, char *buf, size_t n)
{
  return parsecmdline(pid, buf, n);
}

int
proc_cwd(pid_t pid, char *buf, size_t n)
{
  char    path[PATH_MAX];
  ssize_t k;

  snprintf(path, sizeof(path), ARUU_LINUX_PATH_PROC "/%d/cwd", (int)pid);
  if (n == 0 || (k = readlink(path, buf, n - 1)) < 0)
    return -1;
  buf[k] = '\0';
  return 0;
}

void
proc_ttyname(unsigned major, unsigned minor, char *buf, size_t n)
{
  if (major == 136)
    snprintf(buf, n, "pts/%u", minor);
  else if (major == 4 && minor < 64)
    snprintf(buf, n, "tty%u", minor);
  else if (major == 4)
    snprintf(buf, n, "ttyS%u", minor - 64);
  else if (major == 5 && minor == 0)
    snprintf(buf, n, "tty");
  else
    snprintf(buf, n, "?");
}

ssize_t
proc_selfexe(char *buf, size_t n)
{
  return readlink(ARUU_LINUX_PATH_PROC "/self/exe", buf, n);
}

#endif
