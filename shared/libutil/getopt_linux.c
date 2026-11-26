/* see LICENSE file for copyright and license details */
#include "../util.h"

#include <unistd.h>

/* the Linux libcs, musl among them, start over when optind is 0 */
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
void
getopt_reset(void)
{
  optind = 0;
}
#endif
