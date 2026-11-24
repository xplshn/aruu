/* see LICENSE file for copyright and license details */
#include "../session.h"

/* no login records: who and last have nothing to read */
#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
void
session_record(const char *user)
{
  (void)user;
}
#endif
