/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBBLAKE3_H
#define ARUU_LIBBLAKE3_H

#include <stddef.h>
#include <stdint.h>

#define BLAKE3_KEY_LEN   32
#define BLAKE3_OUT_LEN   32
#define BLAKE3_BLOCK_LEN 64
#define BLAKE3_CHUNK_LEN 1024
#define BLAKE3_MAX_DEPTH 54

struct Blake3ChunkState {
  uint32_t cv[8];
  uint64_t chunk_counter;
  uint8_t  buf[BLAKE3_BLOCK_LEN];
  uint8_t  buf_len;
  uint8_t  blocks_compressed;
  uint8_t  flags;
};

struct Blake3Hasher {
  uint32_t                key[8];
  struct Blake3ChunkState chunk;
  uint8_t                 cv_stack_len;
  uint8_t                 cv_stack[(BLAKE3_MAX_DEPTH + 1) * BLAKE3_OUT_LEN];
};

void blake3_hasher_init(struct Blake3Hasher *self);
void blake3_hasher_init_keyed(struct Blake3Hasher *self, const uint8_t key[BLAKE3_KEY_LEN]);
void blake3_hasher_init_derive_key(struct Blake3Hasher *self, const char *context);
void blake3_hasher_init_derive_key_raw(struct Blake3Hasher *self, const void *context,
                                        size_t context_len);
void blake3_hasher_update(struct Blake3Hasher *self, const void *input, size_t input_len);
void blake3_hasher_finalize(const struct Blake3Hasher *self, uint8_t *out, size_t out_len);
void blake3_hasher_finalize_seek(const struct Blake3Hasher *self, uint64_t seek, uint8_t *out,
                                  size_t out_len);
void blake3_hasher_reset(struct Blake3Hasher *self);

const char *blake3_version(void);

#endif
