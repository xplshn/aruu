/* see LICENSE file for copyright and license details */
#ifndef ED25519_H
#define ED25519_H

#include <stddef.h>
#include <stdint.h>

/*
 * ed25519 signatures (rfc 8032), small and constant time where it matters,
 * after tweetnacl. a secret key is the 32 byte seed followed by the public
 * key, as nacl and signify keep it. hashing is aruu's sha512
 */

enum {
  ED25519_SEED = 32,
  ED25519_PUBLIC = 32,
  ED25519_SECRET = 64,
  ED25519_SIG = 64,
};

/* the key pair of seed: sk is seed then pk */
void ed25519_keypair(uint8_t pk[ED25519_PUBLIC], uint8_t sk[ED25519_SECRET],
                     const uint8_t seed[ED25519_SEED]);

/* sig over the n bytes of m */
void ed25519_sign(uint8_t sig[ED25519_SIG], const uint8_t *m, size_t n,
                  const uint8_t sk[ED25519_SECRET]);

/* 0 when sig is pk's over m, -1 otherwise */
int ed25519_verify(const uint8_t sig[ED25519_SIG], const uint8_t *m, size_t n,
                   const uint8_t pk[ED25519_PUBLIC]);

#endif
