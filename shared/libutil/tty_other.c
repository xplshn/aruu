/* see LICENSE file for copyright and license details */
#include "../filesystem.h"
#include "../paths.h"
#include "../util.h"

#include <stddef.h>

/* a kernel with no backend: the numbers are the ones its dev_t holds,
 * and no name is known for them */
#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
void
devtotty(int dev, int *tty_maj, int *tty_min)
{
  *tty_maj = fs_devmajor(dev);
  *tty_min = fs_devminor(dev);
}

int
ttytostr(int tty_maj, int tty_min, char *str, size_t n)
{
  (void)tty_maj;
  (void)tty_min;
  if (n < 2)
    return -1;
  str[0] = '?';
  str[1] = '\0';
  return 0;
}

const char *
ttyconsole(void)
{
  return ARUU_PATH_DEVCONSOLE;
}

const char *
ttyconsterm(void)
{
  return "vt100";
}

void
ttyhangup(void)
{
}
#endif
