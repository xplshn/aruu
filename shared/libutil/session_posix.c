/* see LICENSE file for copyright and license details */
#include "../session.h"

#include <errno.h>

/* no login records: who and last have nothing to read */
#if !defined(__linux__) || defined(ARUU_FORCE_POSIX)
void
session_record(const char *user)
{
  (void)user;
}

int
session_each(const char *path, session_fn *fn, void *arg)
{
  (void)path;
  (void)fn;
  (void)arg;
  errno = ENOTSUP;
  return -1;
}

void
session_clear(const char *line)
{
  (void)line;
}

int
session_lastlog(FILE *fp, uid_t uid, struct SessionEntry *e)
{
  (void)fp;
  (void)uid;
  (void)e;
  errno = ENOTSUP;
  return -1;
}
#endif
