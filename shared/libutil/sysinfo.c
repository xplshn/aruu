#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/sysinfo.h>
#else
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

int
get_uptime(long *uptime_secs)
{
#ifdef __linux__
  struct sysinfo info;

  if (sysinfo(&info) < 0)
    return -1;
  *uptime_secs = info.uptime;
  return 0;
#else
  struct timeval boottime;
  size_t         len    = sizeof(boottime);
  int            mib[2] = {CTL_KERN, KERN_BOOTTIME};
  time_t         now;

  if (sysctl(mib, 2, &boottime, &len, NULL, 0) < 0)
    return -1;
  time(&now);
  *uptime_secs = (long)(now - boottime.tv_sec);
  return 0;
#endif
}

int
get_loads(double loads[3])
{
  if (getloadavg(loads, 3) != 3)
    return -1;
  return 0;
}

int
get_meminfo(struct MemInfo *mi)
{
  memset(mi, 0, sizeof(*mi));
#ifdef __linux__
  struct sysinfo     info;
  unsigned long long unit;

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
#else
  int           mib[2];
  size_t        len;
  unsigned long physmem    = 0;
  unsigned int  page_size  = 4096;
  unsigned int  free_pages = 0;

  mib[0] = CTL_HW;
  mib[1] = HW_PHYSMEM;
  len    = sizeof(physmem);
  if (sysctl(mib, 2, &physmem, &len, NULL, 0) < 0)
    return -1;
  mi->total = physmem;

  mib[0] = CTL_HW;
  mib[1] = HW_PAGESIZE;
  len    = sizeof(page_size);
  sysctl(mib, 2, &page_size, &len, NULL, 0);

  len = sizeof(free_pages);
  if (sysctlbyname("vm.stats.vm.v_free_count", &free_pages, &len, NULL, 0) >= 0) {
    mi->free = (unsigned long long)free_pages * page_size;
  } else {
    mi->free = mi->total / 4;
  }
  return 0;
#endif
}

#ifdef __linux__
#include <sys/klog.h>
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
#else
ssize_t
get_dmesg(char *buf, size_t len)
{
  int    mib[2];
  size_t needed = len;

  mib[0] = CTL_KERN;
  mib[1] = KERN_MSGBUF;
  if (sysctl(mib, 2, buf, &needed, NULL, 0) < 0)
    return -1;
  return (ssize_t)needed;
}

int
clear_dmesg(void)
{
  return -1;
}

int
set_console_level(int level)
{
  (void)level;
  return -1;
}
#endif
