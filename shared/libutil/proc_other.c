/* see LICENSE file for copyright and license details */
#include "../proc.h"
#include "../util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* a kernel with no backend yet: its process table is not read, and every
 * tool that wants it says why. proc_linux.c is the one that reads it */
#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)

int
parsecmdline(pid_t pid, char *buf, size_t siz)
{
  (void)pid;
  (void)buf;
  (void)siz;
  errno = ENOTSUP;
  return -1;
}

int
parsestat(pid_t pid, struct procstat *ps)
{
  (void)pid;
  (void)ps;
  errno = ENOTSUP;
  return -1;
}

int
parsestatus(pid_t pid, struct procstatus *pstatus)
{
  (void)pid;
  (void)pstatus;
  errno = ENOTSUP;
  return -1;
}

int
pidfile(const char *file)
{
  char *end;

  errno = 0;
  strtol(file, &end, 10);
  return *end == '\0' && errno == 0;
}

int
proc_each(proc_fn *fn, void *arg)
{
  (void)fn;
  (void)arg;
  errno = ENOTSUP;
  return -1;
}

int
proc_info(pid_t pid, struct procinfo *p)
{
  (void)pid;
  memset(p, 0, sizeof *p);
  errno = ENOTSUP;
  return -1;
}

int
proc_args(pid_t pid, char *buf, size_t n)
{
  return parsecmdline(pid, buf, n);
}

int
proc_cwd(pid_t pid, char *buf, size_t n)
{
  (void)pid;
  (void)buf;
  (void)n;
  errno = ENOTSUP;
  return -1;
}

void
proc_ttyname(unsigned major, unsigned minor, char *buf, size_t n)
{
  (void)major;
  (void)minor;
  if (n)
    strlcpy(buf, "?", n);
}

ssize_t
proc_selfexe(char *buf, size_t n)
{
  (void)buf;
  (void)n;
  errno = ENOTSUP;
  return -1;
}

#endif
