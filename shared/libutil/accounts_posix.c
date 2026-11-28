/* see LICENSE file for copyright and license details */
#include "../accounts.h"

#include <errno.h>

/* where accounts are kept some other way (a BSD's master.passwd and its
 * database): not here yet */
#if !defined(__linux__) || defined(ARUU_FORCE_POSIX)
int
acct_add(const char *name, const char *home, const char *shell, const char *hash, uid_t *uid)
{
  (void)name;
  (void)home;
  (void)shell;
  (void)hash;
  (void)uid;
  errno = ENOSYS;
  return -1;
}

int
acct_remove(const char *name)
{
  (void)name;
  errno = ENOSYS;
  return -1;
}

int
acct_member(const char *group, const char *name, int in)
{
  (void)group;
  (void)name;
  (void)in;
  errno = ENOSYS;
  return -1;
}
#endif
