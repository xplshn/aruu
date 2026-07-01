/* See LICENSE file for copyright and license details. */
#ifndef ARUU_PASSWD_H
#define ARUU_PASSWD_H

#include <pwd.h>
#include <sys/types.h>

struct pwdb_entry {
  char *name;
  char *hash;
  uid_t uid;
  gid_t gid;
};

int pw_init(void);
int pw_check(const struct passwd *pw, const char *pass);
int pwdb_lookup(struct pwdb_entry *ent, const char *name);

#define PW_SALT_MAX 64
int pw_gensalt(char *salt, size_t salt_sz);
int pw_gensalt_cipher(char *salt, size_t salt_sz, const char *prefix, size_t rand_len);
int pwdb_update(const char *name, const char *newhash);

#endif
