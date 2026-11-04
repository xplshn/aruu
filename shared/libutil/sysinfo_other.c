#include "../sysinfo.h"

#include <errno.h>
#include <string.h>

#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
int
get_uptime(long *uptime_secs)
{
  (void)uptime_secs;
  errno = ENOTSUP;
  return -1;
}

int
get_meminfo(struct MemInfo *mi)
{
  memset(mi, 0, sizeof(*mi));
  errno = ENOTSUP;
  return -1;
}

ssize_t
get_dmesg(char *buf, size_t len)
{
  (void)buf;
  (void)len;
  errno = ENOTSUP;
  return -1;
}

int
clear_dmesg(void)
{
  errno = ENOTSUP;
  return -1;
}

int
set_console_level(int level)
{
  (void)level;
  errno = ENOTSUP;
  return -1;
}
#endif
