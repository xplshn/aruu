/* see LICENSE file for copyright and license details */
#ifndef SIGNIFY_H
#define SIGNIFY_H

#include <stddef.h>
#include <stdint.h>

#include "ed25519.h"

/*
 * openbsd signify's key and signature files, so either program reads what
 * the other wrote: an "untrusted comment:" line, then a line of base64. the
 * keys are ed25519. a secret key is kept unencrypted (kdf rounds 0); one
 * made with a passphrase is refused here, signify -n makes one we take
 */

struct signify_pub {
  uint8_t keynum[8];
  uint8_t pk[ED25519_PUBLIC];
};

struct signify_sec {
  uint8_t keynum[8];
  uint8_t sk[ED25519_SECRET];
};

/* a new key pair into pubpath and secpath, neither there before. 0 when made */
int signify_generate(const char *pubpath, const char *secpath, const char *comment);

/* the key in path. 0 when read, -1 said otherwise */
int signify_read_pub(const char *path, struct signify_pub *out);
int signify_read_sec(const char *path, struct signify_sec *out);

/* the signature of m into sigpath, its comment naming the public key
   pubname to check it with. 0 when written */
int signify_sign(const struct signify_sec *sec, const uint8_t *m, size_t n,
                 const char *pubname, const char *sigpath);

/* 0 when sigpath is pub's signature of m, -1 said otherwise */
int signify_verify(const struct signify_pub *pub, const uint8_t *m, size_t n,
                   const char *sigpath);

#endif
