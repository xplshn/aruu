#if FEATURE_SYSINFO_EXT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include "../paths.h"
#include "../sysinfo.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)
#include <stddef.h>
#include <stdio.h>
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

/* the fields of meminfo free(1) shows, in kB there */
static const struct {
  const char *name;
  size_t      off;
} meminfo_fields[] = {
    {"MemTotal:",     offsetof(struct MemInfo, total)    },
    {"MemFree:",      offsetof(struct MemInfo, free)     },
    {"MemAvailable:", offsetof(struct MemInfo, available)},
    {"Shmem:",        offsetof(struct MemInfo, shared)   },
    {"Buffers:",      offsetof(struct MemInfo, buffers)  },
    {"Cached:",       offsetof(struct MemInfo, cached)   },
    {"SReclaimable:", offsetof(struct MemInfo, cached)   },
    {"SwapTotal:",    offsetof(struct MemInfo, totalswap)},
    {"SwapFree:",     offsetof(struct MemInfo, freeswap) },
};

/* sysinfo(2) knows nothing of the page cache, so this is the fallback */
static int
meminfo_sysinfo(struct MemInfo *mi)
{
  struct sysinfo     info;
  unsigned long long unit;

  if (sysinfo(&info) < 0)
    return -1;
  unit          = info.mem_unit ? info.mem_unit : 1;
  mi->total     = info.totalram * unit;
  mi->free      = info.freeram * unit;
  mi->available = (info.freeram + info.bufferram) * unit;
  mi->shared    = info.sharedram * unit;
  mi->buffers   = info.bufferram * unit;
  mi->totalswap = info.totalswap * unit;
  mi->freeswap  = info.freeswap * unit;
  return 0;
}

int
get_meminfo(struct MemInfo *mi)
{
  FILE              *fp;
  char               line[128];
  unsigned long long kb;
  size_t             i, len;
  int                avail = 0;

  memset(mi, 0, sizeof(*mi));
  if (!(fp = fopen(ARUU_LINUX_PATH_PROC_MEMINFO, "r")))
    return meminfo_sysinfo(mi);
  while (fgets(line, sizeof(line), fp)) {
    for (i = 0; i < LEN(meminfo_fields); i++) {
      len = strlen(meminfo_fields[i].name);
      if (strncmp(line, meminfo_fields[i].name, len) || sscanf(line + len, "%llu", &kb) != 1)
        continue;
      *(unsigned long long *)((char *)mi + meminfo_fields[i].off) += kb * 1024;
      avail |= !strcmp(meminfo_fields[i].name, "MemAvailable:");
    }
  }
  fclose(fp);
  /* kernels before 3.14 have no MemAvailable */
  if (!avail)
    mi->available = mi->free + mi->buffers + mi->cached;
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
