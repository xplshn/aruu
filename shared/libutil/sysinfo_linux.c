#if FEATURE_SYSINFO_EXT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#if defined(__linux__)
#include "../sysinfo.h"

#include <string.h>
#include <sys/klog.h>
#include <sys/sysinfo.h>

int
get_uptime(long *uptime_secs)
{
  struct sysinfo info;

  if (sysinfo(&info) < 0)
    return -1;
  *uptime_secs = info.uptime;
  return 0;
}

int
get_meminfo(struct MemInfo *mi)
{
  struct sysinfo     info;
  unsigned long long unit;

  memset(mi, 0, sizeof(*mi));
  if (sysinfo(&info) < 0)
    return -1;
  unit          = info.mem_unit ? info.mem_unit : 1;
  mi->total     = info.totalram * unit;
  mi->free      = info.freeram * unit;
  mi->shared    = info.sharedram * unit;
  mi->buffers   = info.bufferram * unit;
  mi->cached    = 0;
  mi->totalswap = info.totalswap * unit;
  mi->freeswap  = info.freeswap * unit;
  return 0;
}

ssize_t
get_dmesg(char *buf, size_t len)
{
  if (!buf && len == 0)
    return klogctl(10, NULL, 0);
  return klogctl(3, buf, len);
}

int
clear_dmesg(void)
{
  if (klogctl(5, NULL, 0) < 0)
    return -1;
  return 0;
}

int
set_console_level(int level)
{
  if (klogctl(8, NULL, level) < 0)
    return -1;
  return 0;
}
#endif
