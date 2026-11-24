/* see LICENSE file for copyright and license details */
#include "../passwd.h"

#include <errno.h>
#include <stddef.h>

/* no shadow file: the hash is the one the passwd entry holds */
#if !(defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)   \
      || defined(__DragonFly__))                                                                 \
    || defined(ARUU_FORCE_OTHER)
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
  (void)name;
  (void)newhash;
  return 1;
}
#endif
