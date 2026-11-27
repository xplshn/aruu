/* see LICENSE file for copyright and license details */
#ifndef ARUU_ACCOUNTS_H
#define ARUU_ACCOUNTS_H

#include <sys/types.h>

/* the account files (passwd, shadow, group) changed for a person: each
 * file is written whole, keeping its mode and owner. 0, or -1 with errno
 * set (EEXIST, ENOENT, EINVAL, ENOSYS where the system keeps accounts
 * some other way). passwords go through pwdb_update (passwd.h) */

/* a name a person may have: a lowercase letter or _, then lowercase
 * letters, digits, _ and -, at most 32 */
int acct_valid_name(const char *name);
/* a person: the next free id from 1000, a group of their own of the same
 * id, home and shell as given, hash as their password (crypt's) */
int acct_add(const char *name, const char *home, const char *shell, const char *hash, uid_t *uid);
/* the person gone from the three files, and from every group they were in.
 * their home stays */
int acct_remove(const char *name);
/* name put in group (in != 0) or taken out of it */
int acct_member(const char *group, const char *name, int in);

#endif
