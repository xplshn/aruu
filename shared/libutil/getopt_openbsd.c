/* see LICENSE file for copyright and license details */
#include "../util.h"

#include <unistd.h>

#if defined(__OpenBSD__) && !defined(ARUU_FORCE_OTHER)
extern int optreset;

void
getopt_reset(void)
{
  optreset = 1;
  optind   = 1;
}
#endif
