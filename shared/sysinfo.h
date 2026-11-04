#ifndef SYSINFO_H
#define SYSINFO_H

#include <stddef.h>
#include <sys/types.h>

struct MemInfo {
  unsigned long long total;
  unsigned long long free;
  unsigned long long shared;
  unsigned long long buffers;
  unsigned long long cached;
  unsigned long long totalswap;
  unsigned long long freeswap;
};

int     get_uptime(long *);
int     get_loads(double *);
int     get_meminfo(struct MemInfo *);
ssize_t get_dmesg(char *, size_t);
int     clear_dmesg(void);
int     set_console_level(int);

#endif
