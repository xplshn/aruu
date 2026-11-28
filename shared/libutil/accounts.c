/* see LICENSE file for copyright and license details */
#include "../accounts.h"

#include <ctype.h>
#include <string.h>

#define NAME_MAX_ACCT 32

int
acct_valid_name(const char *name)
{
  size_t n = strlen(name);

  if (n == 0 || n > NAME_MAX_ACCT || !(islower((unsigned char)name[0]) || name[0] == '_'))
    return 0;
  for (; *name; name++)
    if (!islower((unsigned char)*name) && !isdigit((unsigned char)*name) && *name != '_' && *name != '-')
      return 0;
  return 1;
}

