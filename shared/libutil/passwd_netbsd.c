/* see LICENSE file for copyright and license details */
#include "../passwd.h"

#include <errno.h>
#include <stddef.h>

/* getpwnam already returns the hash to root, master.passwd holds it and
 * pwd_mkdb rebuilds the databases from it */
#if defined(__NetBSD__) && !defined(ARUU_FORCE_POSIX)
char *
pw_shadow_hash(const char *name)
{
  (void)name;
  errno = 0;
  return NULL;
}

int
pw_shadow_update(const char *name, const char *newhash)
{
  return pw_update_master(name, newhash);
}
#endif
