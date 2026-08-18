/* see LICENSE file for copyright and license details */

#include <stdio.h>
#include <stdlib.h>

#include "util.h"

int get_meminfo(struct MemInfo *);

static unsigned int mem_unit = 1;
static unsigned int unit_shift;

static unsigned long long
scale(unsigned long long v)
{
  return (v * mem_unit) >> unit_shift;
}

static void
usage(void)
{
  eprintf("usage: %s [-bkmg]\n", argv0);
}

// ?man free: display memory usage
// ?man display the amount of free and used memory in the system
int
main(int argc, char *argv[])
{
  struct MemInfo mi;

  ARGBEGIN
  {
    // ?man -b: specify block size or base directory
    case 'b':
      unit_shift = 0;
      break;
    // ?man -k: specify option flag
    case 'k':
      unit_shift = 10;
      break;
    // ?man -m: specify mode or limit
    case 'm':
      unit_shift = 20;
      break;
    // ?man -g: specify option flag
    case 'g':
      unit_shift = 30;
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc)
    usage();

  if (get_meminfo(&mi) < 0)
    eprintf("get_meminfo:");

  printf("     %13s%13s%13s%13s%13s\n", "total", "used", "free", "shared", "buffers");
  printf("Mem: ");
  printf(
      "%13llu%13llu%13llu%13llu%13llu\n",
      scale(mi.total),
      scale(mi.total - mi.free),
      scale(mi.free),
      scale(mi.shared),
      scale(mi.buffers)
  );
  printf("-/+ buffers/cache:");
  printf("%13llu%13llu\n", scale(mi.total - mi.free - mi.buffers), scale(mi.free + mi.buffers));
  printf("Swap:");
  printf(
      "%13llu%13llu%13llu\n",
      scale(mi.totalswap),
      scale(mi.totalswap - mi.freeswap),
      scale(mi.freeswap)
  );

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    return 1;

  return 0;
}
