#if FEATURE_SYSINFO_EXT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include "../sysinfo.h"

#include <stdlib.h>

int
get_loads(double *loads)
{
  if (getloadavg(loads, 3) != 3)
    return -1;
  return 0;
}
