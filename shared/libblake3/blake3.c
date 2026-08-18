/* see LICENSE file for copyright and license details */

#if defined(__x86_64__) && (defined(__clang__) || (defined(__GNUC__) && !defined(__TINYC__)))
#define BLAKE3_X86_SIMD 1
#else
#define BLAKE3_X86_SIMD 0
#endif

#include "blake3.h"

#include <assert.h>
#if BLAKE3_X86_SIMD
#include <immintrin.h>
#endif
#include <stdint.h>
#include <string.h>

#define BLAKE3_VERSION_STRING "1.5.0"

#if BLAKE3_X86_SIMD
#define MAX_SIMD_DEGREE 16
#else
#define MAX_SIMD_DEGREE 1
#endif

#define MAX_SIMD_DEGREE_OR_2 (MAX_SIMD_DEGREE > 2 ? MAX_SIMD_DEGREE : 2)

enum Blake3Flags {
  CHUNK_START         = 1 << 0,
  CHUNK_END           = 1 << 1,
  PARENT              = 1 << 2,
  ROOT                = 1 << 3,
  KEYED_HASH          = 1 << 4,
  DERIVE_KEY_CONTEXT  = 1 << 5,
  DERIVE_KEY_MATERIAL = 1 << 6
};

struct Output {
  uint32_t input_cv[8];
  uint64_t counter;
  uint8_t  block[BLAKE3_BLOCK_LEN];
  uint8_t  block_len;
  uint8_t  flags;
};

static const uint32_t IV[8] = {
    0x6A09E667UL,
    0xBB67AE85UL,
    0x3C6EF372UL,
    0xA54FF53AUL,
    0x510E527FUL,
    0x9B05688CUL,
    0x1F83D9ABUL,
    0x5BE0CD19UL
};

static const uint8_t MSG_SCHEDULE[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
};

static inline uint32_t
load32(const void *src)
{
  const uint8_t *p = (const uint8_t *)src;

  return ((uint32_t)(p[0]) << 0) | ((uint32_t)(p[1]) << 8) | ((uint32_t)(p[2]) << 16)
         | ((uint32_t)(p[3]) << 24);
}

static inline void
store32(void *dst, uint32_t w)
{
  uint8_t *p = (uint8_t *)dst;

  p[0] = (uint8_t)(w >> 0);
  p[1] = (uint8_t)(w >> 8);
  p[2] = (uint8_t)(w >> 16);
  p[3] = (uint8_t)(w >> 24);
}

static inline void
store_cv_words(uint8_t bytes_out[32], uint32_t cv_words[8])
{
  store32(&bytes_out[0 * 4], cv_words[0]);
  store32(&bytes_out[1 * 4], cv_words[1]);
  store32(&bytes_out[2 * 4], cv_words[2]);
  store32(&bytes_out[3 * 4], cv_words[3]);
  store32(&bytes_out[4 * 4], cv_words[4]);
  store32(&bytes_out[5 * 4], cv_words[5]);
  store32(&bytes_out[6 * 4], cv_words[6]);
  store32(&bytes_out[7 * 4], cv_words[7]);
}

static inline uint32_t
counter_low(uint64_t counter)
{
  return (uint32_t)counter;
}

static inline uint32_t
counter_high(uint64_t counter)
{
  return (uint32_t)(counter >> 32);
}

/* forward declarations */
#if BLAKE3_X86_SIMD
void blake3_compress_in_place_sse2(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
);
void blake3_compress_xof_sse2(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
);
void blake3_hash_many_sse2(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);

void blake3_compress_in_place_sse41(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
);
void blake3_compress_xof_sse41(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
);
void blake3_hash_many_sse41(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);

void blake3_hash_many_avx2(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);

void blake3_compress_in_place_avx512(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
);
void blake3_compress_xof_avx512(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
);
void blake3_hash_many_avx512(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);
#endif

void blake3_compress_in_place_portable(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
);
void blake3_compress_xof_portable(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
);
void blake3_hash_many_portable(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);

void blake3_compress_in_place(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
);
void blake3_compress_xof(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
);
void blake3_hash_many(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
);

/* portable implementations */
static inline uint32_t
rotr32(uint32_t w, uint32_t c)
{
  return (w >> c) | (w << (32 - c));
}

static inline void
g_portable(uint32_t *state, size_t a, size_t b, size_t c, size_t d, uint32_t x, uint32_t y)
{
  state[a] = state[a] + state[b] + x;
  state[d] = rotr32(state[d] ^ state[a], 16);
  state[c] = state[c] + state[d];
  state[b] = rotr32(state[b] ^ state[c], 12);
  state[a] = state[a] + state[b] + y;
  state[d] = rotr32(state[d] ^ state[a], 8);
  state[c] = state[c] + state[d];
  state[b] = rotr32(state[b] ^ state[c], 7);
}

static inline void
round_fn_portable(uint32_t state[16], const uint32_t *msg, size_t round)
{
  const uint8_t *schedule = MSG_SCHEDULE[round];

  g_portable(state, 0, 4, 8, 12, msg[schedule[0]], msg[schedule[1]]);
  g_portable(state, 1, 5, 9, 13, msg[schedule[2]], msg[schedule[3]]);
  g_portable(state, 2, 6, 10, 14, msg[schedule[4]], msg[schedule[5]]);
  g_portable(state, 3, 7, 11, 15, msg[schedule[6]], msg[schedule[7]]);

  g_portable(state, 0, 5, 10, 15, msg[schedule[8]], msg[schedule[9]]);
  g_portable(state, 1, 6, 11, 12, msg[schedule[10]], msg[schedule[11]]);
  g_portable(state, 2, 7, 8, 13, msg[schedule[12]], msg[schedule[13]]);
  g_portable(state, 3, 4, 9, 14, msg[schedule[14]], msg[schedule[15]]);
}

static inline void
compress_pre_portable(
    uint32_t       state[16],
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags
)
{
  uint32_t block_words[16];

  block_words[0]  = load32(block + 4 * 0);
  block_words[1]  = load32(block + 4 * 1);
  block_words[2]  = load32(block + 4 * 2);
  block_words[3]  = load32(block + 4 * 3);
  block_words[4]  = load32(block + 4 * 4);
  block_words[5]  = load32(block + 4 * 5);
  block_words[6]  = load32(block + 4 * 6);
  block_words[7]  = load32(block + 4 * 7);
  block_words[8]  = load32(block + 4 * 8);
  block_words[9]  = load32(block + 4 * 9);
  block_words[10] = load32(block + 4 * 10);
  block_words[11] = load32(block + 4 * 11);
  block_words[12] = load32(block + 4 * 12);
  block_words[13] = load32(block + 4 * 13);
  block_words[14] = load32(block + 4 * 14);
  block_words[15] = load32(block + 4 * 15);

  state[0]  = cv[0];
  state[1]  = cv[1];
  state[2]  = cv[2];
  state[3]  = cv[3];
  state[4]  = cv[4];
  state[5]  = cv[5];
  state[6]  = cv[6];
  state[7]  = cv[7];
  state[8]  = IV[0];
  state[9]  = IV[1];
  state[10] = IV[2];
  state[11] = IV[3];
  state[12] = counter_low(counter);
  state[13] = counter_high(counter);
  state[14] = (uint32_t)block_len;
  state[15] = (uint32_t)flags;

  round_fn_portable(state, &block_words[0], 0);
  round_fn_portable(state, &block_words[0], 1);
  round_fn_portable(state, &block_words[0], 2);
  round_fn_portable(state, &block_words[0], 3);
  round_fn_portable(state, &block_words[0], 4);
  round_fn_portable(state, &block_words[0], 5);
  round_fn_portable(state, &block_words[0], 6);
}

void
blake3_compress_in_place_portable(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
)
{
  uint32_t state[16];

  compress_pre_portable(state, cv, block, block_len, counter, flags);
  cv[0] = state[0] ^ state[8];
  cv[1] = state[1] ^ state[9];
  cv[2] = state[2] ^ state[10];
  cv[3] = state[3] ^ state[11];
  cv[4] = state[4] ^ state[12];
  cv[5] = state[5] ^ state[13];
  cv[6] = state[6] ^ state[14];
  cv[7] = state[7] ^ state[15];
}

void
blake3_compress_xof_portable(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
)
{
  uint32_t state[16];

  compress_pre_portable(state, cv, block, block_len, counter, flags);

  store32(&out[0 * 4], state[0] ^ state[8]);
  store32(&out[1 * 4], state[1] ^ state[9]);
  store32(&out[2 * 4], state[2] ^ state[10]);
  store32(&out[3 * 4], state[3] ^ state[11]);
  store32(&out[4 * 4], state[4] ^ state[12]);
  store32(&out[5 * 4], state[5] ^ state[13]);
  store32(&out[6 * 4], state[6] ^ state[14]);
  store32(&out[7 * 4], state[7] ^ state[15]);
  store32(&out[8 * 4], state[8] ^ cv[0]);
  store32(&out[9 * 4], state[9] ^ cv[1]);
  store32(&out[10 * 4], state[10] ^ cv[2]);
  store32(&out[11 * 4], state[11] ^ cv[3]);
  store32(&out[12 * 4], state[12] ^ cv[4]);
  store32(&out[13 * 4], state[13] ^ cv[5]);
  store32(&out[14 * 4], state[14] ^ cv[6]);
  store32(&out[15 * 4], state[15] ^ cv[7]);
}

static inline void
hash_one_portable(
    const uint8_t *input,
    size_t         blocks,
    const uint32_t key[8],
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        flags_start,
    uint8_t        flags_end,
    uint8_t        out[BLAKE3_OUT_LEN]
)
{
  uint32_t cv[8];
  uint8_t  block_flags;

  memcpy(cv, key, BLAKE3_KEY_LEN);
  block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_portable(cv, input, BLAKE3_BLOCK_LEN, counter, block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  store_cv_words(out, cv);
}

void
blake3_hash_many_portable(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
)
{
  while (num_inputs > 0) {
    hash_one_portable(inputs[0], blocks, key, counter, flags, flags_start, flags_end, out);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out = &out[BLAKE3_OUT_LEN];
  }
}

/* cpu features detection */
enum { SSE2 = 1 << 0, SSE41 = 1 << 1, AVX2 = 1 << 2, AVX512 = 1 << 3 };

static int blake3_cpu_features = 0;
static int blake3_cpu_detected = 0;

#if BLAKE3_X86_SIMD
#include <cpuid.h>

static void
blake3_cpuid(uint32_t out[4], uint32_t id, uint32_t sid)
{
  __cpuid_count(id, sid, out[0], out[1], out[2], out[3]);
}

static uint64_t
blake3_xgetbv(void)
{
  uint32_t eax, edx;

  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return ((uint64_t)edx << 32) | eax;
}
#endif

static void
blake3_detect_cpu_features(void)
{
#if BLAKE3_X86_SIMD
  enum { EAX, EBX, ECX, EDX };
  uint32_t regs[4];
  uint64_t xcr0;
  int      features = 0;

  blake3_cpuid(regs, 1, 0);
  if (regs[EDX] & (1UL << 26))
    features |= SSE2;
  if (regs[ECX] & (1UL << 19))
    features |= SSE41;
  /* osxsave */
  if (regs[ECX] & (1UL << 27)) {
    blake3_cpuid(regs, 0, 0);
    if (regs[EAX] >= 7) {
      blake3_cpuid(regs, 7, 0);
      xcr0 = blake3_xgetbv();
      /* avx2 and xcr0 sse, avx */
      if ((regs[EBX] & (1UL << 5)) && (xcr0 & 0x06) == 0x06)
        features |= AVX2;
      /* avx512f, avx512vl and xcr0 opmask, zmm_hi256,
 * hi16_zmm */
      if ((regs[EBX] & (1UL << 31 | 1UL << 16)) && (xcr0 & 0xe0) == 0xe0)
        features |= AVX512;
    }
  }
  blake3_cpu_features = features;
#endif
  blake3_cpu_detected = 1;
}

#if BLAKE3_X86_SIMD
__attribute__((constructor)) static void
blake3_init_cpu(void)
{
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
}
#endif

#if BLAKE3_X86_SIMD
#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("sse2"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("sse2")
#endif
#define DEGREE_SSE2 4

#define _mm_shuffle_ps2(a, b, c)                                                                   \
  (_mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), (c))))

static inline __m128i
loadu_sse2(const uint8_t src[16])
{
  return _mm_loadu_si128((const __m128i *)src);
}

static inline void
storeu_sse2(__m128i src, uint8_t dest[16])
{
  _mm_storeu_si128((__m128i *)dest, src);
}

static inline __m128i
addv_sse2(__m128i a, __m128i b)
{
  return _mm_add_epi32(a, b);
}

static inline __m128i
xorv_sse2(__m128i a, __m128i b)
{
  return _mm_xor_si128(a, b);
}

static inline __m128i
set1_sse2(uint32_t x)
{
  return _mm_set1_epi32((int32_t)x);
}

static inline __m128i
set4_sse2(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  return _mm_setr_epi32((int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d);
}

static inline __m128i
rot16_sse2(__m128i x)
{
  return _mm_shufflehi_epi16(_mm_shufflelo_epi16(x, 0xB1), 0xB1);
}

static inline __m128i
rot12_sse2(__m128i x)
{
  return xorv_sse2(_mm_srli_epi32(x, 12), _mm_slli_epi32(x, 32 - 12));
}

static inline __m128i
rot8_sse2(__m128i x)
{
  return xorv_sse2(_mm_srli_epi32(x, 8), _mm_slli_epi32(x, 32 - 8));
}

static inline __m128i
rot7_sse2(__m128i x)
{
  return xorv_sse2(_mm_srli_epi32(x, 7), _mm_slli_epi32(x, 32 - 7));
}

static inline void
g1_sse2(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = addv_sse2(addv_sse2(*row0, m), *row1);
  *row3 = xorv_sse2(*row3, *row0);
  *row3 = rot16_sse2(*row3);
  *row2 = addv_sse2(*row2, *row3);
  *row1 = xorv_sse2(*row1, *row2);
  *row1 = rot12_sse2(*row1);
}

static inline void
g2_sse2(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = addv_sse2(addv_sse2(*row0, m), *row1);
  *row3 = xorv_sse2(*row3, *row0);
  *row3 = rot8_sse2(*row3);
  *row2 = addv_sse2(*row2, *row3);
  *row1 = xorv_sse2(*row1, *row2);
  *row1 = rot7_sse2(*row1);
}

static inline void
diagonalize_sse2(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(2, 1, 0, 3));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(0, 3, 2, 1));
}

static inline void
undiagonalize_sse2(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(0, 3, 2, 1));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(2, 1, 0, 3));
}

static inline __m128i
blend_epi16_sse2(__m128i a, __m128i b, const int16_t imm8)
{
  const __m128i bits = _mm_set_epi16(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
  __m128i       mask = _mm_set1_epi16(imm8);

  mask = _mm_and_si128(mask, bits);
  mask = _mm_cmpeq_epi16(mask, bits);
  return _mm_or_si128(_mm_and_si128(mask, b), _mm_andnot_si128(mask, a));
}

static inline void
compress_pre_sse2(
    __m128i        rows[4],
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags
)
{
  __m128i m0, m1, m2, m3;
  __m128i t0, t1, t2, t3, tt;

  rows[0] = loadu_sse2((uint8_t *)&cv[0]);
  rows[1] = loadu_sse2((uint8_t *)&cv[4]);
  rows[2] = set4_sse2(IV[0], IV[1], IV[2], IV[3]);
  rows[3] =
      set4_sse2(counter_low(counter), counter_high(counter), (uint32_t)block_len, (uint32_t)flags);

  m0 = loadu_sse2(&block[sizeof(__m128i) * 0]);
  m1 = loadu_sse2(&block[sizeof(__m128i) * 1]);
  m2 = loadu_sse2(&block[sizeof(__m128i) * 2]);
  m3 = loadu_sse2(&block[sizeof(__m128i) * 3]);

  /* round 1 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(2, 0, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 3, 1));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(2, 0, 2, 0));
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2, 1, 0, 3));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 1, 3, 1));
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(2, 1, 0, 3));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 2 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 3 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 4 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 5 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 6 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 7 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = blend_epi16_sse2(tt, t1, 0xCC);
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse2(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = blend_epi16_sse2(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse2(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse2(&rows[0], &rows[2], &rows[3]);
}

void
blake3_compress_in_place_sse2(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
)
{
  __m128i rows[4];

  compress_pre_sse2(rows, cv, block, block_len, counter, flags);
  storeu_sse2(xorv_sse2(rows[0], rows[2]), (uint8_t *)&cv[0]);
  storeu_sse2(xorv_sse2(rows[1], rows[3]), (uint8_t *)&cv[4]);
}

void
blake3_compress_xof_sse2(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out_buf[64]
)
{
  __m128i rows[4];

  compress_pre_sse2(rows, cv, block, block_len, counter, flags);
  storeu_sse2(xorv_sse2(rows[0], rows[2]), &out_buf[0]);
  storeu_sse2(xorv_sse2(rows[1], rows[3]), &out_buf[16]);
  storeu_sse2(xorv_sse2(rows[2], loadu_sse2((uint8_t *)&cv[0])), &out_buf[32]);
  storeu_sse2(xorv_sse2(rows[3], loadu_sse2((uint8_t *)&cv[4])), &out_buf[48]);
}

static inline void
round_fn_sse2(__m128i v[16], __m128i m[16], size_t r)
{
  v[0]  = addv_sse2(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = addv_sse2(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = addv_sse2(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = addv_sse2(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = addv_sse2(v[0], v[4]);
  v[1]  = addv_sse2(v[1], v[5]);
  v[2]  = addv_sse2(v[2], v[6]);
  v[3]  = addv_sse2(v[3], v[7]);
  v[12] = xorv_sse2(v[12], v[0]);
  v[13] = xorv_sse2(v[13], v[1]);
  v[14] = xorv_sse2(v[14], v[2]);
  v[15] = xorv_sse2(v[15], v[3]);
  v[12] = rot16_sse2(v[12]);
  v[13] = rot16_sse2(v[13]);
  v[14] = rot16_sse2(v[14]);
  v[15] = rot16_sse2(v[15]);
  v[8]  = addv_sse2(v[8], v[12]);
  v[9]  = addv_sse2(v[9], v[13]);
  v[10] = addv_sse2(v[10], v[14]);
  v[11] = addv_sse2(v[11], v[15]);
  v[4]  = xorv_sse2(v[4], v[8]);
  v[5]  = xorv_sse2(v[5], v[9]);
  v[6]  = xorv_sse2(v[6], v[10]);
  v[7]  = xorv_sse2(v[7], v[11]);
  v[4]  = rot12_sse2(v[4]);
  v[5]  = rot12_sse2(v[5]);
  v[6]  = rot12_sse2(v[6]);
  v[7]  = rot12_sse2(v[7]);
  v[0]  = addv_sse2(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = addv_sse2(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = addv_sse2(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = addv_sse2(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = addv_sse2(v[0], v[4]);
  v[1]  = addv_sse2(v[1], v[5]);
  v[2]  = addv_sse2(v[2], v[6]);
  v[3]  = addv_sse2(v[3], v[7]);
  v[12] = xorv_sse2(v[12], v[0]);
  v[13] = xorv_sse2(v[13], v[1]);
  v[14] = xorv_sse2(v[14], v[2]);
  v[15] = xorv_sse2(v[15], v[3]);
  v[12] = rot8_sse2(v[12]);
  v[13] = rot8_sse2(v[13]);
  v[14] = rot8_sse2(v[14]);
  v[15] = rot8_sse2(v[15]);
  v[8]  = addv_sse2(v[8], v[12]);
  v[9]  = addv_sse2(v[9], v[13]);
  v[10] = addv_sse2(v[10], v[14]);
  v[11] = addv_sse2(v[11], v[15]);
  v[4]  = xorv_sse2(v[4], v[8]);
  v[5]  = xorv_sse2(v[5], v[9]);
  v[6]  = xorv_sse2(v[6], v[10]);
  v[7]  = xorv_sse2(v[7], v[11]);
  v[4]  = rot7_sse2(v[4]);
  v[5]  = rot7_sse2(v[5]);
  v[6]  = rot7_sse2(v[6]);
  v[7]  = rot7_sse2(v[7]);

  v[0]  = addv_sse2(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = addv_sse2(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = addv_sse2(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = addv_sse2(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = addv_sse2(v[0], v[5]);
  v[1]  = addv_sse2(v[1], v[6]);
  v[2]  = addv_sse2(v[2], v[7]);
  v[3]  = addv_sse2(v[3], v[4]);
  v[15] = xorv_sse2(v[15], v[0]);
  v[12] = xorv_sse2(v[12], v[1]);
  v[13] = xorv_sse2(v[13], v[2]);
  v[14] = xorv_sse2(v[14], v[3]);
  v[15] = rot16_sse2(v[15]);
  v[12] = rot16_sse2(v[12]);
  v[13] = rot16_sse2(v[13]);
  v[14] = rot16_sse2(v[14]);
  v[10] = addv_sse2(v[10], v[15]);
  v[11] = addv_sse2(v[11], v[12]);
  v[8]  = addv_sse2(v[8], v[13]);
  v[9]  = addv_sse2(v[9], v[14]);
  v[5]  = xorv_sse2(v[5], v[10]);
  v[6]  = xorv_sse2(v[6], v[11]);
  v[7]  = xorv_sse2(v[7], v[8]);
  v[4]  = xorv_sse2(v[4], v[9]);
  v[5]  = rot12_sse2(v[5]);
  v[6]  = rot12_sse2(v[6]);
  v[7]  = rot12_sse2(v[7]);
  v[4]  = rot12_sse2(v[4]);
  v[0]  = addv_sse2(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = addv_sse2(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = addv_sse2(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = addv_sse2(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = addv_sse2(v[0], v[5]);
  v[1]  = addv_sse2(v[1], v[6]);
  v[2]  = addv_sse2(v[2], v[7]);
  v[3]  = addv_sse2(v[3], v[4]);
  v[15] = xorv_sse2(v[15], v[0]);
  v[12] = xorv_sse2(v[12], v[1]);
  v[13] = xorv_sse2(v[13], v[2]);
  v[14] = xorv_sse2(v[14], v[3]);
  v[15] = rot8_sse2(v[15]);
  v[12] = rot8_sse2(v[12]);
  v[13] = rot8_sse2(v[13]);
  v[14] = rot8_sse2(v[14]);
  v[10] = addv_sse2(v[10], v[15]);
  v[11] = addv_sse2(v[11], v[12]);
  v[8]  = addv_sse2(v[8], v[13]);
  v[9]  = addv_sse2(v[9], v[14]);
  v[5]  = xorv_sse2(v[5], v[10]);
  v[6]  = xorv_sse2(v[6], v[11]);
  v[7]  = xorv_sse2(v[7], v[8]);
  v[4]  = xorv_sse2(v[4], v[9]);
  v[5]  = rot7_sse2(v[5]);
  v[6]  = rot7_sse2(v[6]);
  v[7]  = rot7_sse2(v[7]);
  v[4]  = rot7_sse2(v[4]);
}

static inline void
transpose_vecs_sse2(__m128i vecs[DEGREE_SSE2])
{
  __m128i ab_01 = _mm_unpacklo_epi32(vecs[0], vecs[1]);
  __m128i ab_23 = _mm_unpackhi_epi32(vecs[0], vecs[1]);
  __m128i cd_01 = _mm_unpacklo_epi32(vecs[2], vecs[3]);
  __m128i cd_23 = _mm_unpackhi_epi32(vecs[2], vecs[3]);

  __m128i abcd_0 = _mm_unpacklo_epi64(ab_01, cd_01);
  __m128i abcd_1 = _mm_unpackhi_epi64(ab_01, cd_01);
  __m128i abcd_2 = _mm_unpacklo_epi64(ab_23, cd_23);
  __m128i abcd_3 = _mm_unpackhi_epi64(ab_23, cd_23);

  vecs[0] = abcd_0;
  vecs[1] = abcd_1;
  vecs[2] = abcd_2;
  vecs[3] = abcd_3;
}

static inline void
transpose_msg_vecs_sse2(const uint8_t *const *inputs, size_t block_offset, __m128i out_msg[16])
{
  size_t i;

  out_msg[0]  = loadu_sse2(&inputs[0][block_offset + 0 * sizeof(__m128i)]);
  out_msg[1]  = loadu_sse2(&inputs[1][block_offset + 0 * sizeof(__m128i)]);
  out_msg[2]  = loadu_sse2(&inputs[2][block_offset + 0 * sizeof(__m128i)]);
  out_msg[3]  = loadu_sse2(&inputs[3][block_offset + 0 * sizeof(__m128i)]);
  out_msg[4]  = loadu_sse2(&inputs[0][block_offset + 1 * sizeof(__m128i)]);
  out_msg[5]  = loadu_sse2(&inputs[1][block_offset + 1 * sizeof(__m128i)]);
  out_msg[6]  = loadu_sse2(&inputs[2][block_offset + 1 * sizeof(__m128i)]);
  out_msg[7]  = loadu_sse2(&inputs[3][block_offset + 1 * sizeof(__m128i)]);
  out_msg[8]  = loadu_sse2(&inputs[0][block_offset + 2 * sizeof(__m128i)]);
  out_msg[9]  = loadu_sse2(&inputs[1][block_offset + 2 * sizeof(__m128i)]);
  out_msg[10] = loadu_sse2(&inputs[2][block_offset + 2 * sizeof(__m128i)]);
  out_msg[11] = loadu_sse2(&inputs[3][block_offset + 2 * sizeof(__m128i)]);
  out_msg[12] = loadu_sse2(&inputs[0][block_offset + 3 * sizeof(__m128i)]);
  out_msg[13] = loadu_sse2(&inputs[1][block_offset + 3 * sizeof(__m128i)]);
  out_msg[14] = loadu_sse2(&inputs[2][block_offset + 3 * sizeof(__m128i)]);
  out_msg[15] = loadu_sse2(&inputs[3][block_offset + 3 * sizeof(__m128i)]);

  for (i = 0; i < 4; i++) {
    _mm_prefetch((const void *)&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs_sse2(&out_msg[0]);
  transpose_vecs_sse2(&out_msg[4]);
  transpose_vecs_sse2(&out_msg[8]);
  transpose_vecs_sse2(&out_msg[12]);
}

static inline void
load_counters_sse2(uint64_t counter, int increment_counter, __m128i *out_lo, __m128i *out_hi)
{
  const __m128i mask  = _mm_set1_epi32(-increment_counter);
  const __m128i add0  = _mm_set_epi32(3, 2, 1, 0);
  const __m128i add1  = _mm_and_si128(mask, add0);
  __m128i       l     = _mm_add_epi32(_mm_set1_epi32((int32_t)counter), add1);
  __m128i       carry = _mm_cmpgt_epi32(
      _mm_xor_si128(add1, _mm_set1_epi32(0x80000000)), _mm_xor_si128(l, _mm_set1_epi32(0x80000000))
  );
  __m128i h = _mm_sub_epi32(_mm_set1_epi32((int32_t)(counter >> 32)), carry);

  *out_lo = l;
  *out_hi = h;
}

void
blake3_hash4_sse2(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m128i h_vecs[8];
  __m128i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_sse2(key[0]);
  h_vecs[1] = set1_sse2(key[1]);
  h_vecs[2] = set1_sse2(key[2]);
  h_vecs[3] = set1_sse2(key[3]);
  h_vecs[4] = set1_sse2(key[4]);
  h_vecs[5] = set1_sse2(key[5]);
  h_vecs[6] = set1_sse2(key[6]);
  h_vecs[7] = set1_sse2(key[7]);

  load_counters_sse2(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m128i block_len_vec;
    __m128i block_flags_vec;
    __m128i msg_vecs[16];
    __m128i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_sse2(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_sse2(block_flags);
    transpose_msg_vecs_sse2(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_sse2(IV[0]);
    v[9]  = set1_sse2(IV[1]);
    v[10] = set1_sse2(IV[2]);
    v[11] = set1_sse2(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn_sse2(v, msg_vecs, 0);
    round_fn_sse2(v, msg_vecs, 1);
    round_fn_sse2(v, msg_vecs, 2);
    round_fn_sse2(v, msg_vecs, 3);
    round_fn_sse2(v, msg_vecs, 4);
    round_fn_sse2(v, msg_vecs, 5);
    round_fn_sse2(v, msg_vecs, 6);

    h_vecs[0] = xorv_sse2(v[0], v[8]);
    h_vecs[1] = xorv_sse2(v[1], v[9]);
    h_vecs[2] = xorv_sse2(v[2], v[10]);
    h_vecs[3] = xorv_sse2(v[3], v[11]);
    h_vecs[4] = xorv_sse2(v[4], v[12]);
    h_vecs[5] = xorv_sse2(v[5], v[13]);
    h_vecs[6] = xorv_sse2(v[6], v[14]);
    h_vecs[7] = xorv_sse2(v[7], v[15]);

    block_flags = flags;
  }

  transpose_vecs_sse2(&h_vecs[0]);
  transpose_vecs_sse2(&h_vecs[4]);
  storeu_sse2(h_vecs[0], &out_bytes[0 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[4], &out_bytes[1 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[1], &out_bytes[2 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[5], &out_bytes[3 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[2], &out_bytes[4 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[6], &out_bytes[5 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[3], &out_bytes[6 * sizeof(__m128i)]);
  storeu_sse2(h_vecs[7], &out_bytes[7 * sizeof(__m128i)]);
}

static inline void
hash_one_sse2(
    const uint8_t *input,
    size_t         blocks,
    const uint32_t key[8],
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        flags_start,
    uint8_t        flags_end,
    uint8_t        out_bytes[BLAKE3_OUT_LEN]
)
{
  uint32_t cv[8];
  uint8_t  block_flags;

  memcpy(cv, key, BLAKE3_KEY_LEN);
  block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_sse2(cv, input, BLAKE3_BLOCK_LEN, counter, block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  memcpy(out_bytes, cv, BLAKE3_OUT_LEN);
}

void
blake3_hash_many_sse2(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  while (num_inputs >= DEGREE_SSE2) {
    blake3_hash4_sse2(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += DEGREE_SSE2;
    }
    inputs += DEGREE_SSE2;
    num_inputs -= DEGREE_SSE2;
    out_bytes = &out_bytes[DEGREE_SSE2 * BLAKE3_OUT_LEN];
  }
  while (num_inputs > 0) {
    hash_one_sse2(inputs[0], blocks, key, counter, flags, flags_start, flags_end, out_bytes);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out_bytes = &out_bytes[BLAKE3_OUT_LEN];
  }
}
#if defined(__clang__)
#pragma clang attribute pop
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("sse4.1"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("sse4.1")
#endif
#define DEGREE_SSE41 4

#define _mm_shuffle_ps2_sse41(a, b, c)                                                             \
  (_mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), (c))))

static inline __m128i
loadu_sse41(const uint8_t src[16])
{
  return _mm_loadu_si128((const __m128i *)src);
}

static inline void
storeu_sse41(__m128i src, uint8_t dest[16])
{
  _mm_storeu_si128((__m128i *)dest, src);
}

static inline __m128i
addv_sse41(__m128i a, __m128i b)
{
  return _mm_add_epi32(a, b);
}

static inline __m128i
xorv_sse41(__m128i a, __m128i b)
{
  return _mm_xor_si128(a, b);
}

static inline __m128i
set1_sse41(uint32_t x)
{
  return _mm_set1_epi32((int32_t)x);
}

static inline __m128i
set4_sse41(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  return _mm_setr_epi32((int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d);
}

static inline __m128i
rot16_sse41(__m128i x)
{
  return _mm_shuffle_epi8(x, _mm_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2));
}

static inline __m128i
rot12_sse41(__m128i x)
{
  return xorv_sse41(_mm_srli_epi32(x, 12), _mm_slli_epi32(x, 32 - 12));
}

static inline __m128i
rot8_sse41(__m128i x)
{
  return _mm_shuffle_epi8(x, _mm_set_epi8(12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1));
}

static inline __m128i
rot7_sse41(__m128i x)
{
  return xorv_sse41(_mm_srli_epi32(x, 7), _mm_slli_epi32(x, 32 - 7));
}

static inline void
g1_sse41(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = addv_sse41(addv_sse41(*row0, m), *row1);
  *row3 = xorv_sse41(*row3, *row0);
  *row3 = rot16_sse41(*row3);
  *row2 = addv_sse41(*row2, *row3);
  *row1 = xorv_sse41(*row1, *row2);
  *row1 = rot12_sse41(*row1);
}

static inline void
g2_sse41(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = addv_sse41(addv_sse41(*row0, m), *row1);
  *row3 = xorv_sse41(*row3, *row0);
  *row3 = rot8_sse41(*row3);
  *row2 = addv_sse41(*row2, *row3);
  *row1 = xorv_sse41(*row1, *row2);
  *row1 = rot7_sse41(*row1);
}

static inline void
diagonalize_sse41(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(2, 1, 0, 3));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(0, 3, 2, 1));
}

static inline void
undiagonalize_sse41(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(0, 3, 2, 1));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(2, 1, 0, 3));
}

static inline void
compress_pre_sse41(
    __m128i        rows[4],
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags
)
{
  __m128i m0, m1, m2, m3;
  __m128i t0, t1, t2, t3, tt;

  rows[0] = loadu_sse41((uint8_t *)&cv[0]);
  rows[1] = loadu_sse41((uint8_t *)&cv[4]);
  rows[2] = set4_sse41(IV[0], IV[1], IV[2], IV[3]);
  rows[3] =
      set4_sse41(counter_low(counter), counter_high(counter), (uint32_t)block_len, (uint32_t)flags);

  m0 = loadu_sse41(&block[sizeof(__m128i) * 0]);
  m1 = loadu_sse41(&block[sizeof(__m128i) * 1]);
  m2 = loadu_sse41(&block[sizeof(__m128i) * 2]);
  m3 = loadu_sse41(&block[sizeof(__m128i) * 3]);

  /* round 1 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(2, 0, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 3, 1));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(2, 0, 2, 0));
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2, 1, 0, 3));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 1, 3, 1));
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(2, 1, 0, 3));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 2 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 3 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 4 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 5 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 6 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 7 */
  t0 = _mm_shuffle_ps2_sse41(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2_sse41(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  tt = _mm_shuffle_epi32(m0, _MM_SHUFFLE(0, 0, 3, 3));
  t1 = _mm_blend_epi16(tt, t1, 0xCC);
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_sse41(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  tt = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(1, 3, 2, 0));
  g1_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  tt = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(tt, _MM_SHUFFLE(0, 1, 3, 2));
  g2_sse41(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_sse41(&rows[0], &rows[2], &rows[3]);
}

void
blake3_compress_in_place_sse41(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
)
{
  __m128i rows[4];

  compress_pre_sse41(rows, cv, block, block_len, counter, flags);
  storeu_sse41(xorv_sse41(rows[0], rows[2]), (uint8_t *)&cv[0]);
  storeu_sse41(xorv_sse41(rows[1], rows[3]), (uint8_t *)&cv[4]);
}

void
blake3_compress_xof_sse41(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out_buf[64]
)
{
  __m128i rows[4];

  compress_pre_sse41(rows, cv, block, block_len, counter, flags);
  storeu_sse41(xorv_sse41(rows[0], rows[2]), &out_buf[0]);
  storeu_sse41(xorv_sse41(rows[1], rows[3]), &out_buf[16]);
  storeu_sse41(xorv_sse41(rows[2], loadu_sse41((uint8_t *)&cv[0])), &out_buf[32]);
  storeu_sse41(xorv_sse41(rows[3], loadu_sse41((uint8_t *)&cv[4])), &out_buf[48]);
}

static inline void
round_fn_sse41(__m128i v[16], __m128i m[16], size_t r)
{
  v[0]  = addv_sse41(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = addv_sse41(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = addv_sse41(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = addv_sse41(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = addv_sse41(v[0], v[4]);
  v[1]  = addv_sse41(v[1], v[5]);
  v[2]  = addv_sse41(v[2], v[6]);
  v[3]  = addv_sse41(v[3], v[7]);
  v[12] = xorv_sse41(v[12], v[0]);
  v[13] = xorv_sse41(v[13], v[1]);
  v[14] = xorv_sse41(v[14], v[2]);
  v[15] = xorv_sse41(v[15], v[3]);
  v[12] = rot16_sse41(v[12]);
  v[13] = rot16_sse41(v[13]);
  v[14] = rot16_sse41(v[14]);
  v[15] = rot16_sse41(v[15]);
  v[8]  = addv_sse41(v[8], v[12]);
  v[9]  = addv_sse41(v[9], v[13]);
  v[10] = addv_sse41(v[10], v[14]);
  v[11] = addv_sse41(v[11], v[15]);
  v[4]  = xorv_sse41(v[4], v[8]);
  v[5]  = xorv_sse41(v[5], v[9]);
  v[6]  = xorv_sse41(v[6], v[10]);
  v[7]  = xorv_sse41(v[7], v[11]);
  v[4]  = rot12_sse41(v[4]);
  v[5]  = rot12_sse41(v[5]);
  v[6]  = rot12_sse41(v[6]);
  v[7]  = rot12_sse41(v[7]);
  v[0]  = addv_sse41(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = addv_sse41(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = addv_sse41(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = addv_sse41(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = addv_sse41(v[0], v[4]);
  v[1]  = addv_sse41(v[1], v[5]);
  v[2]  = addv_sse41(v[2], v[6]);
  v[3]  = addv_sse41(v[3], v[7]);
  v[12] = xorv_sse41(v[12], v[0]);
  v[13] = xorv_sse41(v[13], v[1]);
  v[14] = xorv_sse41(v[14], v[2]);
  v[15] = xorv_sse41(v[15], v[3]);
  v[12] = rot8_sse41(v[12]);
  v[13] = rot8_sse41(v[13]);
  v[14] = rot8_sse41(v[14]);
  v[15] = rot8_sse41(v[15]);
  v[8]  = addv_sse41(v[8], v[12]);
  v[9]  = addv_sse41(v[9], v[13]);
  v[10] = addv_sse41(v[10], v[14]);
  v[11] = addv_sse41(v[11], v[15]);
  v[4]  = xorv_sse41(v[4], v[8]);
  v[5]  = xorv_sse41(v[5], v[9]);
  v[6]  = xorv_sse41(v[6], v[10]);
  v[7]  = xorv_sse41(v[7], v[11]);
  v[4]  = rot7_sse41(v[4]);
  v[5]  = rot7_sse41(v[5]);
  v[6]  = rot7_sse41(v[6]);
  v[7]  = rot7_sse41(v[7]);

  v[0]  = addv_sse41(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = addv_sse41(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = addv_sse41(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = addv_sse41(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = addv_sse41(v[0], v[5]);
  v[1]  = addv_sse41(v[1], v[6]);
  v[2]  = addv_sse41(v[2], v[7]);
  v[3]  = addv_sse41(v[3], v[4]);
  v[15] = xorv_sse41(v[15], v[0]);
  v[12] = xorv_sse41(v[12], v[1]);
  v[13] = xorv_sse41(v[13], v[2]);
  v[14] = xorv_sse41(v[14], v[3]);
  v[15] = rot16_sse41(v[15]);
  v[12] = rot16_sse41(v[12]);
  v[13] = rot16_sse41(v[13]);
  v[14] = rot16_sse41(v[14]);
  v[10] = addv_sse41(v[10], v[15]);
  v[11] = addv_sse41(v[11], v[12]);
  v[8]  = addv_sse41(v[8], v[13]);
  v[9]  = addv_sse41(v[9], v[14]);
  v[5]  = xorv_sse41(v[5], v[10]);
  v[6]  = xorv_sse41(v[6], v[11]);
  v[7]  = xorv_sse41(v[7], v[8]);
  v[4]  = xorv_sse41(v[4], v[9]);
  v[5]  = rot12_sse41(v[5]);
  v[6]  = rot12_sse41(v[6]);
  v[7]  = rot12_sse41(v[7]);
  v[4]  = rot12_sse41(v[4]);
  v[0]  = addv_sse41(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = addv_sse41(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = addv_sse41(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = addv_sse41(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = addv_sse41(v[0], v[5]);
  v[1]  = addv_sse41(v[1], v[6]);
  v[2]  = addv_sse41(v[2], v[7]);
  v[3]  = addv_sse41(v[3], v[4]);
  v[15] = xorv_sse41(v[15], v[0]);
  v[12] = xorv_sse41(v[12], v[1]);
  v[13] = xorv_sse41(v[13], v[2]);
  v[14] = xorv_sse41(v[14], v[3]);
  v[15] = rot8_sse41(v[15]);
  v[12] = rot8_sse41(v[12]);
  v[13] = rot8_sse41(v[13]);
  v[14] = rot8_sse41(v[14]);
  v[10] = addv_sse41(v[10], v[15]);
  v[11] = addv_sse41(v[11], v[12]);
  v[8]  = addv_sse41(v[8], v[13]);
  v[9]  = addv_sse41(v[9], v[14]);
  v[5]  = xorv_sse41(v[5], v[10]);
  v[6]  = xorv_sse41(v[6], v[11]);
  v[7]  = xorv_sse41(v[7], v[8]);
  v[4]  = xorv_sse41(v[4], v[9]);
  v[5]  = rot7_sse41(v[5]);
  v[6]  = rot7_sse41(v[6]);
  v[7]  = rot7_sse41(v[7]);
  v[4]  = rot7_sse41(v[4]);
}

static inline void
transpose_vecs_sse41(__m128i vecs[DEGREE_SSE41])
{
  __m128i ab_01 = _mm_unpacklo_epi32(vecs[0], vecs[1]);
  __m128i ab_23 = _mm_unpackhi_epi32(vecs[0], vecs[1]);
  __m128i cd_01 = _mm_unpacklo_epi32(vecs[2], vecs[3]);
  __m128i cd_23 = _mm_unpackhi_epi32(vecs[2], vecs[3]);

  __m128i abcd_0 = _mm_unpacklo_epi64(ab_01, cd_01);
  __m128i abcd_1 = _mm_unpackhi_epi64(ab_01, cd_01);
  __m128i abcd_2 = _mm_unpacklo_epi64(ab_23, cd_23);
  __m128i abcd_3 = _mm_unpackhi_epi64(ab_23, cd_23);

  vecs[0] = abcd_0;
  vecs[1] = abcd_1;
  vecs[2] = abcd_2;
  vecs[3] = abcd_3;
}

static inline void
transpose_msg_vecs_sse41(const uint8_t *const *inputs, size_t block_offset, __m128i out_msg[16])
{
  size_t i;

  out_msg[0]  = loadu_sse41(&inputs[0][block_offset + 0 * sizeof(__m128i)]);
  out_msg[1]  = loadu_sse41(&inputs[1][block_offset + 0 * sizeof(__m128i)]);
  out_msg[2]  = loadu_sse41(&inputs[2][block_offset + 0 * sizeof(__m128i)]);
  out_msg[3]  = loadu_sse41(&inputs[3][block_offset + 0 * sizeof(__m128i)]);
  out_msg[4]  = loadu_sse41(&inputs[0][block_offset + 1 * sizeof(__m128i)]);
  out_msg[5]  = loadu_sse41(&inputs[1][block_offset + 1 * sizeof(__m128i)]);
  out_msg[6]  = loadu_sse41(&inputs[2][block_offset + 1 * sizeof(__m128i)]);
  out_msg[7]  = loadu_sse41(&inputs[3][block_offset + 1 * sizeof(__m128i)]);
  out_msg[8]  = loadu_sse41(&inputs[0][block_offset + 2 * sizeof(__m128i)]);
  out_msg[9]  = loadu_sse41(&inputs[1][block_offset + 2 * sizeof(__m128i)]);
  out_msg[10] = loadu_sse41(&inputs[2][block_offset + 2 * sizeof(__m128i)]);
  out_msg[11] = loadu_sse41(&inputs[3][block_offset + 2 * sizeof(__m128i)]);
  out_msg[12] = loadu_sse41(&inputs[0][block_offset + 3 * sizeof(__m128i)]);
  out_msg[13] = loadu_sse41(&inputs[1][block_offset + 3 * sizeof(__m128i)]);
  out_msg[14] = loadu_sse41(&inputs[2][block_offset + 3 * sizeof(__m128i)]);
  out_msg[15] = loadu_sse41(&inputs[3][block_offset + 3 * sizeof(__m128i)]);

  for (i = 0; i < 4; i++) {
    _mm_prefetch((const void *)&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs_sse41(&out_msg[0]);
  transpose_vecs_sse41(&out_msg[4]);
  transpose_vecs_sse41(&out_msg[8]);
  transpose_vecs_sse41(&out_msg[12]);
}

static inline void
load_counters_sse41(uint64_t counter, int increment_counter, __m128i *out_lo, __m128i *out_hi)
{
  const __m128i mask  = _mm_set1_epi32(-increment_counter);
  const __m128i add0  = _mm_set_epi32(3, 2, 1, 0);
  const __m128i add1  = _mm_and_si128(mask, add0);
  __m128i       l     = _mm_add_epi32(_mm_set1_epi32((int32_t)counter), add1);
  __m128i       carry = _mm_cmpgt_epi32(
      _mm_xor_si128(add1, _mm_set1_epi32(0x80000000)), _mm_xor_si128(l, _mm_set1_epi32(0x80000000))
  );
  __m128i h = _mm_sub_epi32(_mm_set1_epi32((int32_t)(counter >> 32)), carry);

  *out_lo = l;
  *out_hi = h;
}

void
blake3_hash4_sse41(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m128i h_vecs[8];
  __m128i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_sse41(key[0]);
  h_vecs[1] = set1_sse41(key[1]);
  h_vecs[2] = set1_sse41(key[2]);
  h_vecs[3] = set1_sse41(key[3]);
  h_vecs[4] = set1_sse41(key[4]);
  h_vecs[5] = set1_sse41(key[5]);
  h_vecs[6] = set1_sse41(key[6]);
  h_vecs[7] = set1_sse41(key[7]);

  load_counters_sse41(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m128i block_len_vec;
    __m128i block_flags_vec;
    __m128i msg_vecs[16];
    __m128i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_sse41(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_sse41(block_flags);
    transpose_msg_vecs_sse41(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_sse41(IV[0]);
    v[9]  = set1_sse41(IV[1]);
    v[10] = set1_sse41(IV[2]);
    v[11] = set1_sse41(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn_sse41(v, msg_vecs, 0);
    round_fn_sse41(v, msg_vecs, 1);
    round_fn_sse41(v, msg_vecs, 2);
    round_fn_sse41(v, msg_vecs, 3);
    round_fn_sse41(v, msg_vecs, 4);
    round_fn_sse41(v, msg_vecs, 5);
    round_fn_sse41(v, msg_vecs, 6);

    h_vecs[0] = xorv_sse41(v[0], v[8]);
    h_vecs[1] = xorv_sse41(v[1], v[9]);
    h_vecs[2] = xorv_sse41(v[2], v[10]);
    h_vecs[3] = xorv_sse41(v[3], v[11]);
    h_vecs[4] = xorv_sse41(v[4], v[12]);
    h_vecs[5] = xorv_sse41(v[5], v[13]);
    h_vecs[6] = xorv_sse41(v[6], v[14]);
    h_vecs[7] = xorv_sse41(v[7], v[15]);

    block_flags = flags;
  }

  transpose_vecs_sse41(&h_vecs[0]);
  transpose_vecs_sse41(&h_vecs[4]);
  storeu_sse41(h_vecs[0], &out_bytes[0 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[4], &out_bytes[1 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[1], &out_bytes[2 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[5], &out_bytes[3 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[2], &out_bytes[4 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[6], &out_bytes[5 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[3], &out_bytes[6 * sizeof(__m128i)]);
  storeu_sse41(h_vecs[7], &out_bytes[7 * sizeof(__m128i)]);
}

static inline void
hash_one_sse41(
    const uint8_t *input,
    size_t         blocks,
    const uint32_t key[8],
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        flags_start,
    uint8_t        flags_end,
    uint8_t        out_bytes[BLAKE3_OUT_LEN]
)
{
  uint32_t cv[8];
  uint8_t  block_flags;

  memcpy(cv, key, BLAKE3_KEY_LEN);
  block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_sse41(cv, input, BLAKE3_BLOCK_LEN, counter, block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  memcpy(out_bytes, cv, BLAKE3_OUT_LEN);
}

void
blake3_hash_many_sse41(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  while (num_inputs >= DEGREE_SSE41) {
    blake3_hash4_sse41(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += DEGREE_SSE41;
    }
    inputs += DEGREE_SSE41;
    num_inputs -= DEGREE_SSE41;
    out_bytes = &out_bytes[DEGREE_SSE41 * BLAKE3_OUT_LEN];
  }
  while (num_inputs > 0) {
    hash_one_sse41(inputs[0], blocks, key, counter, flags, flags_start, flags_end, out_bytes);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out_bytes = &out_bytes[BLAKE3_OUT_LEN];
  }
}
#if defined(__clang__)
#pragma clang attribute pop
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("avx2"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("avx2")
#endif
#define DEGREE_AVX2 8

static inline __m256i
loadu_avx2(const uint8_t src[32])
{
  return _mm256_loadu_si256((const __m256i *)src);
}

static inline void
storeu_avx2(__m256i src, uint8_t dest[32])
{
  _mm256_storeu_si256((__m256i *)dest, src);
}

static inline __m256i
addv_avx2(__m256i a, __m256i b)
{
  return _mm256_add_epi32(a, b);
}

static inline __m256i
xorv_avx2(__m256i a, __m256i b)
{
  return _mm256_xor_si256(a, b);
}

static inline __m256i
set1_avx2(uint32_t x)
{
  return _mm256_set1_epi32((int32_t)x);
}

static inline __m256i
rot16_avx2(__m256i x)
{
  return _mm256_shuffle_epi8(
      x,
      _mm256_set_epi8(
          13,
          12,
          15,
          14,
          9,
          8,
          11,
          10,
          5,
          4,
          7,
          6,
          1,
          0,
          3,
          2,
          13,
          12,
          15,
          14,
          9,
          8,
          11,
          10,
          5,
          4,
          7,
          6,
          1,
          0,
          3,
          2
      )
  );
}

static inline __m256i
rot12_avx2(__m256i x)
{
  return _mm256_or_si256(_mm256_srli_epi32(x, 12), _mm256_slli_epi32(x, 32 - 12));
}

static inline __m256i
rot8_avx2(__m256i x)
{
  return _mm256_shuffle_epi8(
      x,
      _mm256_set_epi8(
          12,
          15,
          14,
          13,
          8,
          11,
          10,
          9,
          4,
          7,
          6,
          5,
          0,
          3,
          2,
          1,
          12,
          15,
          14,
          13,
          8,
          11,
          10,
          9,
          4,
          7,
          6,
          5,
          0,
          3,
          2,
          1
      )
  );
}

static inline __m256i
rot7_avx2(__m256i x)
{
  return _mm256_or_si256(_mm256_srli_epi32(x, 7), _mm256_slli_epi32(x, 32 - 7));
}

static inline void
round_fn_avx2(__m256i v[16], __m256i m[16], size_t r)
{
  v[0]  = addv_avx2(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = addv_avx2(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = addv_avx2(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = addv_avx2(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = addv_avx2(v[0], v[4]);
  v[1]  = addv_avx2(v[1], v[5]);
  v[2]  = addv_avx2(v[2], v[6]);
  v[3]  = addv_avx2(v[3], v[7]);
  v[12] = xorv_avx2(v[12], v[0]);
  v[13] = xorv_avx2(v[13], v[1]);
  v[14] = xorv_avx2(v[14], v[2]);
  v[15] = xorv_avx2(v[15], v[3]);
  v[12] = rot16_avx2(v[12]);
  v[13] = rot16_avx2(v[13]);
  v[14] = rot16_avx2(v[14]);
  v[15] = rot16_avx2(v[15]);
  v[8]  = addv_avx2(v[8], v[12]);
  v[9]  = addv_avx2(v[9], v[13]);
  v[10] = addv_avx2(v[10], v[14]);
  v[11] = addv_avx2(v[11], v[15]);
  v[4]  = xorv_avx2(v[4], v[8]);
  v[5]  = xorv_avx2(v[5], v[9]);
  v[6]  = xorv_avx2(v[6], v[10]);
  v[7]  = xorv_avx2(v[7], v[11]);
  v[4]  = rot12_avx2(v[4]);
  v[5]  = rot12_avx2(v[5]);
  v[6]  = rot12_avx2(v[6]);
  v[7]  = rot12_avx2(v[7]);
  v[0]  = addv_avx2(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = addv_avx2(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = addv_avx2(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = addv_avx2(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = addv_avx2(v[0], v[4]);
  v[1]  = addv_avx2(v[1], v[5]);
  v[2]  = addv_avx2(v[2], v[6]);
  v[3]  = addv_avx2(v[3], v[7]);
  v[12] = xorv_avx2(v[12], v[0]);
  v[13] = xorv_avx2(v[13], v[1]);
  v[14] = xorv_avx2(v[14], v[2]);
  v[15] = xorv_avx2(v[15], v[3]);
  v[12] = rot8_avx2(v[12]);
  v[13] = rot8_avx2(v[13]);
  v[14] = rot8_avx2(v[14]);
  v[15] = rot8_avx2(v[15]);
  v[8]  = addv_avx2(v[8], v[12]);
  v[9]  = addv_avx2(v[9], v[13]);
  v[10] = addv_avx2(v[10], v[14]);
  v[11] = addv_avx2(v[11], v[15]);
  v[4]  = xorv_avx2(v[4], v[8]);
  v[5]  = xorv_avx2(v[5], v[9]);
  v[6]  = xorv_avx2(v[6], v[10]);
  v[7]  = xorv_avx2(v[7], v[11]);
  v[4]  = rot7_avx2(v[4]);
  v[5]  = rot7_avx2(v[5]);
  v[6]  = rot7_avx2(v[6]);
  v[7]  = rot7_avx2(v[7]);

  v[0]  = addv_avx2(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = addv_avx2(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = addv_avx2(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = addv_avx2(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = addv_avx2(v[0], v[5]);
  v[1]  = addv_avx2(v[1], v[6]);
  v[2]  = addv_avx2(v[2], v[7]);
  v[3]  = addv_avx2(v[3], v[4]);
  v[15] = xorv_avx2(v[15], v[0]);
  v[12] = xorv_avx2(v[12], v[1]);
  v[13] = xorv_avx2(v[13], v[2]);
  v[14] = xorv_avx2(v[14], v[3]);
  v[15] = rot16_avx2(v[15]);
  v[12] = rot16_avx2(v[12]);
  v[13] = rot16_avx2(v[13]);
  v[14] = rot16_avx2(v[14]);
  v[10] = addv_avx2(v[10], v[15]);
  v[11] = addv_avx2(v[11], v[12]);
  v[8]  = addv_avx2(v[8], v[13]);
  v[9]  = addv_avx2(v[9], v[14]);
  v[5]  = xorv_avx2(v[5], v[10]);
  v[6]  = xorv_avx2(v[6], v[11]);
  v[7]  = xorv_avx2(v[7], v[8]);
  v[4]  = xorv_avx2(v[4], v[9]);
  v[5]  = rot12_avx2(v[5]);
  v[6]  = rot12_avx2(v[6]);
  v[7]  = rot12_avx2(v[7]);
  v[4]  = rot12_avx2(v[4]);
  v[0]  = addv_avx2(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = addv_avx2(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = addv_avx2(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = addv_avx2(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = addv_avx2(v[0], v[5]);
  v[1]  = addv_avx2(v[1], v[6]);
  v[2]  = addv_avx2(v[2], v[7]);
  v[3]  = addv_avx2(v[3], v[4]);
  v[15] = xorv_avx2(v[15], v[0]);
  v[12] = xorv_avx2(v[12], v[1]);
  v[13] = xorv_avx2(v[13], v[2]);
  v[14] = xorv_avx2(v[14], v[3]);
  v[15] = rot8_avx2(v[15]);
  v[12] = rot8_avx2(v[12]);
  v[13] = rot8_avx2(v[13]);
  v[14] = rot8_avx2(v[14]);
  v[10] = addv_avx2(v[10], v[15]);
  v[11] = addv_avx2(v[11], v[12]);
  v[8]  = addv_avx2(v[8], v[13]);
  v[9]  = addv_avx2(v[9], v[14]);
  v[5]  = xorv_avx2(v[5], v[10]);
  v[6]  = xorv_avx2(v[6], v[11]);
  v[7]  = xorv_avx2(v[7], v[8]);
  v[4]  = xorv_avx2(v[4], v[9]);
  v[5]  = rot7_avx2(v[5]);
  v[6]  = rot7_avx2(v[6]);
  v[7]  = rot7_avx2(v[7]);
  v[4]  = rot7_avx2(v[4]);
}

static inline void
transpose_vecs_avx2(__m256i vecs[DEGREE_AVX2])
{
  __m256i ab_0145 = _mm256_unpacklo_epi32(vecs[0], vecs[1]);
  __m256i ab_2367 = _mm256_unpackhi_epi32(vecs[0], vecs[1]);
  __m256i cd_0145 = _mm256_unpacklo_epi32(vecs[2], vecs[3]);
  __m256i cd_2367 = _mm256_unpackhi_epi32(vecs[2], vecs[3]);
  __m256i ef_0145 = _mm256_unpacklo_epi32(vecs[4], vecs[5]);
  __m256i ef_2367 = _mm256_unpackhi_epi32(vecs[4], vecs[5]);
  __m256i gh_0145 = _mm256_unpacklo_epi32(vecs[6], vecs[7]);
  __m256i gh_2367 = _mm256_unpackhi_epi32(vecs[6], vecs[7]);

  __m256i abcd_04 = _mm256_unpacklo_epi64(ab_0145, cd_0145);
  __m256i abcd_15 = _mm256_unpackhi_epi64(ab_0145, cd_0145);
  __m256i abcd_26 = _mm256_unpacklo_epi64(ab_2367, cd_2367);
  __m256i abcd_37 = _mm256_unpackhi_epi64(ab_2367, cd_2367);
  __m256i efgh_04 = _mm256_unpacklo_epi64(ef_0145, gh_0145);
  __m256i efgh_15 = _mm256_unpackhi_epi64(ef_0145, gh_0145);
  __m256i efgh_26 = _mm256_unpacklo_epi64(ef_2367, gh_2367);
  __m256i efgh_37 = _mm256_unpackhi_epi64(ef_2367, gh_2367);

  vecs[0] = _mm256_permute2x128_si256(abcd_04, efgh_04, 0x20);
  vecs[1] = _mm256_permute2x128_si256(abcd_15, efgh_15, 0x20);
  vecs[2] = _mm256_permute2x128_si256(abcd_26, efgh_26, 0x20);
  vecs[3] = _mm256_permute2x128_si256(abcd_37, efgh_37, 0x20);
  vecs[4] = _mm256_permute2x128_si256(abcd_04, efgh_04, 0x31);
  vecs[5] = _mm256_permute2x128_si256(abcd_15, efgh_15, 0x31);
  vecs[6] = _mm256_permute2x128_si256(abcd_26, efgh_26, 0x31);
  vecs[7] = _mm256_permute2x128_si256(abcd_37, efgh_37, 0x31);
}

static inline void
transpose_msg_vecs_avx2(const uint8_t *const *inputs, size_t block_offset, __m256i out_msg[16])
{
  size_t i;

  out_msg[0]  = loadu_avx2(&inputs[0][block_offset + 0 * sizeof(__m256i)]);
  out_msg[1]  = loadu_avx2(&inputs[1][block_offset + 0 * sizeof(__m256i)]);
  out_msg[2]  = loadu_avx2(&inputs[2][block_offset + 0 * sizeof(__m256i)]);
  out_msg[3]  = loadu_avx2(&inputs[3][block_offset + 0 * sizeof(__m256i)]);
  out_msg[4]  = loadu_avx2(&inputs[4][block_offset + 0 * sizeof(__m256i)]);
  out_msg[5]  = loadu_avx2(&inputs[5][block_offset + 0 * sizeof(__m256i)]);
  out_msg[6]  = loadu_avx2(&inputs[6][block_offset + 0 * sizeof(__m256i)]);
  out_msg[7]  = loadu_avx2(&inputs[7][block_offset + 0 * sizeof(__m256i)]);
  out_msg[8]  = loadu_avx2(&inputs[0][block_offset + 1 * sizeof(__m256i)]);
  out_msg[9]  = loadu_avx2(&inputs[1][block_offset + 1 * sizeof(__m256i)]);
  out_msg[10] = loadu_avx2(&inputs[2][block_offset + 1 * sizeof(__m256i)]);
  out_msg[11] = loadu_avx2(&inputs[3][block_offset + 1 * sizeof(__m256i)]);
  out_msg[12] = loadu_avx2(&inputs[4][block_offset + 1 * sizeof(__m256i)]);
  out_msg[13] = loadu_avx2(&inputs[5][block_offset + 1 * sizeof(__m256i)]);
  out_msg[14] = loadu_avx2(&inputs[6][block_offset + 1 * sizeof(__m256i)]);
  out_msg[15] = loadu_avx2(&inputs[7][block_offset + 1 * sizeof(__m256i)]);

  for (i = 0; i < 8; i++) {
    _mm_prefetch((const void *)&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs_avx2(&out_msg[0]);
  transpose_vecs_avx2(&out_msg[8]);
}

static inline void
load_counters_avx2(uint64_t counter, int increment_counter, __m256i *out_lo, __m256i *out_hi)
{
  const __m256i mask  = _mm256_set1_epi32(-increment_counter);
  const __m256i add0  = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
  const __m256i add1  = _mm256_and_si256(mask, add0);
  __m256i       l     = _mm256_add_epi32(_mm256_set1_epi32((int32_t)counter), add1);
  __m256i       carry = _mm256_xor_si256(add1, _mm256_set1_epi32(0x80000000));
  __m256i       comp  = _mm256_xor_si256(l, _mm256_set1_epi32(0x80000000));
  __m256i       gt    = _mm256_cmpgt_epi32(carry, comp);
  __m256i       h     = _mm256_sub_epi32(_mm256_set1_epi32((int32_t)(counter >> 32)), gt);

  *out_lo = l;
  *out_hi = h;
}

void
blake3_hash8_avx2(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m256i h_vecs[8];
  __m256i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_avx2(key[0]);
  h_vecs[1] = set1_avx2(key[1]);
  h_vecs[2] = set1_avx2(key[2]);
  h_vecs[3] = set1_avx2(key[3]);
  h_vecs[4] = set1_avx2(key[4]);
  h_vecs[5] = set1_avx2(key[5]);
  h_vecs[6] = set1_avx2(key[6]);
  h_vecs[7] = set1_avx2(key[7]);

  load_counters_avx2(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m256i block_len_vec;
    __m256i block_flags_vec;
    __m256i msg_vecs[16];
    __m256i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_avx2(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_avx2(block_flags);
    transpose_msg_vecs_avx2(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_avx2(IV[0]);
    v[9]  = set1_avx2(IV[1]);
    v[10] = set1_avx2(IV[2]);
    v[11] = set1_avx2(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn_avx2(v, msg_vecs, 0);
    round_fn_avx2(v, msg_vecs, 1);
    round_fn_avx2(v, msg_vecs, 2);
    round_fn_avx2(v, msg_vecs, 3);
    round_fn_avx2(v, msg_vecs, 4);
    round_fn_avx2(v, msg_vecs, 5);
    round_fn_avx2(v, msg_vecs, 6);

    h_vecs[0] = xorv_avx2(v[0], v[8]);
    h_vecs[1] = xorv_avx2(v[1], v[9]);
    h_vecs[2] = xorv_avx2(v[2], v[10]);
    h_vecs[3] = xorv_avx2(v[3], v[11]);
    h_vecs[4] = xorv_avx2(v[4], v[12]);
    h_vecs[5] = xorv_avx2(v[5], v[13]);
    h_vecs[6] = xorv_avx2(v[6], v[14]);
    h_vecs[7] = xorv_avx2(v[7], v[15]);

    block_flags = flags;
  }

  transpose_vecs_avx2(h_vecs);
  storeu_avx2(h_vecs[0], &out_bytes[0 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[1], &out_bytes[1 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[2], &out_bytes[2 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[3], &out_bytes[3 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[4], &out_bytes[4 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[5], &out_bytes[5 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[6], &out_bytes[6 * sizeof(__m256i)]);
  storeu_avx2(h_vecs[7], &out_bytes[7 * sizeof(__m256i)]);
}

void
blake3_hash_many_avx2(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  while (num_inputs >= DEGREE_AVX2) {
    blake3_hash8_avx2(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += DEGREE_AVX2;
    }
    inputs += DEGREE_AVX2;
    num_inputs -= DEGREE_AVX2;
    out_bytes = &out_bytes[DEGREE_AVX2 * BLAKE3_OUT_LEN];
  }
  blake3_hash_many_sse41(
      inputs,
      num_inputs,
      blocks,
      key,
      counter,
      increment_counter,
      flags,
      flags_start,
      flags_end,
      out_bytes
  );
}
#if defined(__clang__)
#pragma clang attribute pop
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif

#if defined(__clang__)
#pragma clang attribute push(__attribute__((target("avx512f,avx512vl"))), apply_to = function)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC target("avx512f,avx512vl")
#endif
static inline __m128i
loadu_128_avx512(const uint8_t src[16])
{
  return _mm_loadu_si128((const __m128i *)src);
}

static inline __m256i
loadu_256_avx512(const uint8_t src[32])
{
  return _mm256_loadu_si256((const __m256i *)src);
}

static inline __m512i
loadu_512_avx512(const uint8_t src[64])
{
  return _mm512_loadu_si512((const __m512i *)src);
}

static inline void
storeu_128_avx512(__m128i src, uint8_t dest[16])
{
  _mm_storeu_si128((__m128i *)dest, src);
}

static inline void
storeu_256_avx512(__m256i src, uint8_t dest[32])
{
  _mm256_storeu_si256((__m256i *)dest, src);
}

static inline __m128i
add_128_avx512(__m128i a, __m128i b)
{
  return _mm_add_epi32(a, b);
}

static inline __m256i
add_256_avx512(__m256i a, __m256i b)
{
  return _mm256_add_epi32(a, b);
}

static inline __m512i
add_512_avx512(__m512i a, __m512i b)
{
  return _mm512_add_epi32(a, b);
}

static inline __m128i
xor_128_avx512(__m128i a, __m128i b)
{
  return _mm_xor_si128(a, b);
}

static inline __m256i
xor_256_avx512(__m256i a, __m256i b)
{
  return _mm256_xor_si256(a, b);
}

static inline __m512i
xor_512_avx512(__m512i a, __m512i b)
{
  return _mm512_xor_si512(a, b);
}

static inline __m128i
set1_128_avx512(uint32_t x)
{
  return _mm_set1_epi32((int32_t)x);
}

static inline __m256i
set1_256_avx512(uint32_t x)
{
  return _mm256_set1_epi32((int32_t)x);
}

static inline __m512i
set1_512_avx512(uint32_t x)
{
  return _mm512_set1_epi32((int32_t)x);
}

static inline __m128i
set4_avx512(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  return _mm_setr_epi32((int32_t)a, (int32_t)b, (int32_t)c, (int32_t)d);
}

static inline __m128i
rot16_128_avx512(__m128i x)
{
  return _mm_ror_epi32(x, 16);
}

static inline __m256i
rot16_256_avx512(__m256i x)
{
  return _mm256_ror_epi32(x, 16);
}

static inline __m512i
rot16_512_avx512(__m512i x)
{
  return _mm512_ror_epi32(x, 16);
}

static inline __m128i
rot12_128_avx512(__m128i x)
{
  return _mm_ror_epi32(x, 12);
}

static inline __m256i
rot12_256_avx512(__m256i x)
{
  return _mm256_ror_epi32(x, 12);
}

static inline __m512i
rot12_512_avx512(__m512i x)
{
  return _mm512_ror_epi32(x, 12);
}

static inline __m128i
rot8_128_avx512(__m128i x)
{
  return _mm_ror_epi32(x, 8);
}

static inline __m256i
rot8_256_avx512(__m256i x)
{
  return _mm256_ror_epi32(x, 8);
}

static inline __m512i
rot8_512_avx512(__m512i x)
{
  return _mm512_ror_epi32(x, 8);
}

static inline __m128i
rot7_128_avx512(__m128i x)
{
  return _mm_ror_epi32(x, 7);
}

static inline __m256i
rot7_256_avx512(__m256i x)
{
  return _mm256_ror_epi32(x, 7);
}

static inline __m512i
rot7_512_avx512(__m512i x)
{
  return _mm512_ror_epi32(x, 7);
}

static inline void
g1_avx512(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = add_128_avx512(add_128_avx512(*row0, m), *row1);
  *row3 = xor_128_avx512(*row3, *row0);
  *row3 = rot16_128_avx512(*row3);
  *row2 = add_128_avx512(*row2, *row3);
  *row1 = xor_128_avx512(*row1, *row2);
  *row1 = rot12_128_avx512(*row1);
}

static inline void
g2_avx512(__m128i *row0, __m128i *row1, __m128i *row2, __m128i *row3, __m128i m)
{
  *row0 = add_128_avx512(add_128_avx512(*row0, m), *row1);
  *row3 = xor_128_avx512(*row3, *row0);
  *row3 = rot8_128_avx512(*row3);
  *row2 = add_128_avx512(*row2, *row3);
  *row1 = xor_128_avx512(*row1, *row2);
  *row1 = rot7_128_avx512(*row1);
}

static inline void
diagonalize_avx512(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(2, 1, 0, 3));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(0, 3, 2, 1));
}

static inline void
undiagonalize_avx512(__m128i *row0, __m128i *row2, __m128i *row3)
{
  *row0 = _mm_shuffle_epi32(*row0, _MM_SHUFFLE(0, 3, 2, 1));
  *row3 = _mm_shuffle_epi32(*row3, _MM_SHUFFLE(1, 0, 3, 2));
  *row2 = _mm_shuffle_epi32(*row2, _MM_SHUFFLE(2, 1, 0, 3));
}

static inline void
compress_pre_avx512(
    __m128i        rows[4],
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags
)
{
  __m128i m0, m1, m2, m3;
  __m128i t0, t1, t2, t3;

  rows[0] = loadu_128_avx512((const uint8_t *)&cv[0]);
  rows[1] = loadu_128_avx512((const uint8_t *)&cv[4]);
  rows[2] = set4_avx512(IV[0], IV[1], IV[2], IV[3]);
  rows[3] = set4_avx512(
      counter_low(counter), counter_high(counter), (uint32_t)block_len, (uint32_t)flags
  );

  m0 = loadu_128_avx512(&block[sizeof(__m128i) * 0]);
  m1 = loadu_128_avx512(&block[sizeof(__m128i) * 1]);
  m2 = loadu_128_avx512(&block[sizeof(__m128i) * 2]);
  m3 = loadu_128_avx512(&block[sizeof(__m128i) * 3]);

  /* round 1 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(2, 0, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 3, 1));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(2, 0, 2, 0));
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(2, 1, 0, 3));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 1, 3, 1));
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(2, 1, 0, 3));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 2 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 3 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 4 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 5 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 6 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  m0 = t0;
  m1 = t1;
  m2 = t2;
  m3 = t3;

  /* round 7 */
  t0 = _mm_shuffle_ps2(m0, m1, _MM_SHUFFLE(3, 1, 1, 2));
  t0 = _mm_shuffle_epi32(t0, _MM_SHUFFLE(0, 3, 2, 1));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t0);
  t1 = _mm_shuffle_ps2(m2, m3, _MM_SHUFFLE(3, 3, 2, 2));
  t1 = _mm_blend_epi16(m0, t1, 0xCC);
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t1);
  diagonalize_avx512(&rows[0], &rows[2], &rows[3]);
  t2 = _mm_unpacklo_epi64(m3, m1);
  t2 = _mm_blend_epi16(t2, m2, 0xC0);
  t2 = _mm_shuffle_epi32(t2, _MM_SHUFFLE(1, 3, 2, 0));
  g1_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t2);
  t3 = _mm_unpackhi_epi32(m1, m3);
  t3 = _mm_unpacklo_epi32(m2, t3);
  t3 = _mm_shuffle_epi32(t3, _MM_SHUFFLE(0, 1, 3, 2));
  g2_avx512(&rows[0], &rows[1], &rows[2], &rows[3], t3);
  undiagonalize_avx512(&rows[0], &rows[2], &rows[3]);
}

void
blake3_compress_in_place_avx512(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
)
{
  __m128i rows[4];

  compress_pre_avx512(rows, cv, block, block_len, counter, flags);
  storeu_128_avx512(xor_128_avx512(rows[0], rows[2]), (uint8_t *)&cv[0]);
  storeu_128_avx512(xor_128_avx512(rows[1], rows[3]), (uint8_t *)&cv[4]);
}

void
blake3_compress_xof_avx512(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out_buf[64]
)
{
  __m128i rows[4];

  compress_pre_avx512(rows, cv, block, block_len, counter, flags);
  storeu_128_avx512(xor_128_avx512(rows[0], rows[2]), &out_buf[0]);
  storeu_128_avx512(xor_128_avx512(rows[1], rows[3]), &out_buf[16]);
  storeu_128_avx512(
      xor_128_avx512(rows[2], loadu_128_avx512((const uint8_t *)&cv[0])), &out_buf[32]
  );
  storeu_128_avx512(
      xor_128_avx512(rows[3], loadu_128_avx512((const uint8_t *)&cv[4])), &out_buf[48]
  );
}

static inline void
round_fn4_avx512(__m128i v[16], __m128i m[16], size_t r)
{
  v[0]  = add_128_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = add_128_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = add_128_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = add_128_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = add_128_avx512(v[0], v[4]);
  v[1]  = add_128_avx512(v[1], v[5]);
  v[2]  = add_128_avx512(v[2], v[6]);
  v[3]  = add_128_avx512(v[3], v[7]);
  v[12] = xor_128_avx512(v[12], v[0]);
  v[13] = xor_128_avx512(v[13], v[1]);
  v[14] = xor_128_avx512(v[14], v[2]);
  v[15] = xor_128_avx512(v[15], v[3]);
  v[12] = rot16_128_avx512(v[12]);
  v[13] = rot16_128_avx512(v[13]);
  v[14] = rot16_128_avx512(v[14]);
  v[15] = rot16_128_avx512(v[15]);
  v[8]  = add_128_avx512(v[8], v[12]);
  v[9]  = add_128_avx512(v[9], v[13]);
  v[10] = add_128_avx512(v[10], v[14]);
  v[11] = add_128_avx512(v[11], v[15]);
  v[4]  = xor_128_avx512(v[4], v[8]);
  v[5]  = xor_128_avx512(v[5], v[9]);
  v[6]  = xor_128_avx512(v[6], v[10]);
  v[7]  = xor_128_avx512(v[7], v[11]);
  v[4]  = rot12_128_avx512(v[4]);
  v[5]  = rot12_128_avx512(v[5]);
  v[6]  = rot12_128_avx512(v[6]);
  v[7]  = rot12_128_avx512(v[7]);
  v[0]  = add_128_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = add_128_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = add_128_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = add_128_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = add_128_avx512(v[0], v[4]);
  v[1]  = add_128_avx512(v[1], v[5]);
  v[2]  = add_128_avx512(v[2], v[6]);
  v[3]  = add_128_avx512(v[3], v[7]);
  v[12] = xor_128_avx512(v[12], v[0]);
  v[13] = xor_128_avx512(v[13], v[1]);
  v[14] = xor_128_avx512(v[14], v[2]);
  v[15] = xor_128_avx512(v[15], v[3]);
  v[12] = rot8_128_avx512(v[12]);
  v[13] = rot8_128_avx512(v[13]);
  v[14] = rot8_128_avx512(v[14]);
  v[15] = rot8_128_avx512(v[15]);
  v[8]  = add_128_avx512(v[8], v[12]);
  v[9]  = add_128_avx512(v[9], v[13]);
  v[10] = add_128_avx512(v[10], v[14]);
  v[11] = add_128_avx512(v[11], v[15]);
  v[4]  = xor_128_avx512(v[4], v[8]);
  v[5]  = xor_128_avx512(v[5], v[9]);
  v[6]  = xor_128_avx512(v[6], v[10]);
  v[7]  = xor_128_avx512(v[7], v[11]);
  v[4]  = rot7_128_avx512(v[4]);
  v[5]  = rot7_128_avx512(v[5]);
  v[6]  = rot7_128_avx512(v[6]);
  v[7]  = rot7_128_avx512(v[7]);

  v[0]  = add_128_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = add_128_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = add_128_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = add_128_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = add_128_avx512(v[0], v[5]);
  v[1]  = add_128_avx512(v[1], v[6]);
  v[2]  = add_128_avx512(v[2], v[7]);
  v[3]  = add_128_avx512(v[3], v[4]);
  v[15] = xor_128_avx512(v[15], v[0]);
  v[12] = xor_128_avx512(v[12], v[1]);
  v[13] = xor_128_avx512(v[13], v[2]);
  v[14] = xor_128_avx512(v[14], v[3]);
  v[15] = rot16_128_avx512(v[15]);
  v[12] = rot16_128_avx512(v[12]);
  v[13] = rot16_128_avx512(v[13]);
  v[14] = rot16_128_avx512(v[14]);
  v[10] = add_128_avx512(v[10], v[15]);
  v[11] = add_128_avx512(v[11], v[12]);
  v[8]  = add_128_avx512(v[8], v[13]);
  v[9]  = add_128_avx512(v[9], v[14]);
  v[5]  = xor_128_avx512(v[5], v[10]);
  v[6]  = xor_128_avx512(v[6], v[11]);
  v[7]  = xor_128_avx512(v[7], v[8]);
  v[4]  = xor_128_avx512(v[4], v[9]);
  v[5]  = rot12_128_avx512(v[5]);
  v[6]  = rot12_128_avx512(v[6]);
  v[7]  = rot12_128_avx512(v[7]);
  v[4]  = rot12_128_avx512(v[4]);
  v[0]  = add_128_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = add_128_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = add_128_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = add_128_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = add_128_avx512(v[0], v[5]);
  v[1]  = add_128_avx512(v[1], v[6]);
  v[2]  = add_128_avx512(v[2], v[7]);
  v[3]  = add_128_avx512(v[3], v[4]);
  v[15] = xor_128_avx512(v[15], v[0]);
  v[12] = xor_128_avx512(v[12], v[1]);
  v[13] = xor_128_avx512(v[13], v[2]);
  v[14] = xor_128_avx512(v[14], v[3]);
  v[15] = rot8_128_avx512(v[15]);
  v[12] = rot8_128_avx512(v[12]);
  v[13] = rot8_128_avx512(v[13]);
  v[14] = rot8_128_avx512(v[14]);
  v[10] = add_128_avx512(v[10], v[15]);
  v[11] = add_128_avx512(v[11], v[12]);
  v[8]  = add_128_avx512(v[8], v[13]);
  v[9]  = add_128_avx512(v[9], v[14]);
  v[5]  = xor_128_avx512(v[5], v[10]);
  v[6]  = xor_128_avx512(v[6], v[11]);
  v[7]  = xor_128_avx512(v[7], v[8]);
  v[4]  = xor_128_avx512(v[4], v[9]);
  v[5]  = rot7_128_avx512(v[5]);
  v[6]  = rot7_128_avx512(v[6]);
  v[7]  = rot7_128_avx512(v[7]);
  v[4]  = rot7_128_avx512(v[4]);
}

static inline void
round_fn8_avx512(__m256i v[16], __m256i m[16], size_t r)
{
  v[0]  = add_256_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = add_256_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = add_256_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = add_256_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = add_256_avx512(v[0], v[4]);
  v[1]  = add_256_avx512(v[1], v[5]);
  v[2]  = add_256_avx512(v[2], v[6]);
  v[3]  = add_256_avx512(v[3], v[7]);
  v[12] = xor_256_avx512(v[12], v[0]);
  v[13] = xor_256_avx512(v[13], v[1]);
  v[14] = xor_256_avx512(v[14], v[2]);
  v[15] = xor_256_avx512(v[15], v[3]);
  v[12] = rot16_256_avx512(v[12]);
  v[13] = rot16_256_avx512(v[13]);
  v[14] = rot16_256_avx512(v[14]);
  v[15] = rot16_256_avx512(v[15]);
  v[8]  = add_256_avx512(v[8], v[12]);
  v[9]  = add_256_avx512(v[9], v[13]);
  v[10] = add_256_avx512(v[10], v[14]);
  v[11] = add_256_avx512(v[11], v[15]);
  v[4]  = xor_256_avx512(v[4], v[8]);
  v[5]  = xor_256_avx512(v[5], v[9]);
  v[6]  = xor_256_avx512(v[6], v[10]);
  v[7]  = xor_256_avx512(v[7], v[11]);
  v[4]  = rot12_256_avx512(v[4]);
  v[5]  = rot12_256_avx512(v[5]);
  v[6]  = rot12_256_avx512(v[6]);
  v[7]  = rot12_256_avx512(v[7]);
  v[0]  = add_256_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = add_256_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = add_256_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = add_256_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = add_256_avx512(v[0], v[4]);
  v[1]  = add_256_avx512(v[1], v[5]);
  v[2]  = add_256_avx512(v[2], v[6]);
  v[3]  = add_256_avx512(v[3], v[7]);
  v[12] = xor_256_avx512(v[12], v[0]);
  v[13] = xor_256_avx512(v[13], v[1]);
  v[14] = xor_256_avx512(v[14], v[2]);
  v[15] = xor_256_avx512(v[15], v[3]);
  v[12] = rot8_256_avx512(v[12]);
  v[13] = rot8_256_avx512(v[13]);
  v[14] = rot8_256_avx512(v[14]);
  v[15] = rot8_256_avx512(v[15]);
  v[8]  = add_256_avx512(v[8], v[12]);
  v[9]  = add_256_avx512(v[9], v[13]);
  v[10] = add_256_avx512(v[10], v[14]);
  v[11] = add_256_avx512(v[11], v[15]);
  v[4]  = xor_256_avx512(v[4], v[8]);
  v[5]  = xor_256_avx512(v[5], v[9]);
  v[6]  = xor_256_avx512(v[6], v[10]);
  v[7]  = xor_256_avx512(v[7], v[11]);
  v[4]  = rot7_256_avx512(v[4]);
  v[5]  = rot7_256_avx512(v[5]);
  v[6]  = rot7_256_avx512(v[6]);
  v[7]  = rot7_256_avx512(v[7]);

  v[0]  = add_256_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = add_256_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = add_256_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = add_256_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = add_256_avx512(v[0], v[5]);
  v[1]  = add_256_avx512(v[1], v[6]);
  v[2]  = add_256_avx512(v[2], v[7]);
  v[3]  = add_256_avx512(v[3], v[4]);
  v[15] = xor_256_avx512(v[15], v[0]);
  v[12] = xor_256_avx512(v[12], v[1]);
  v[13] = xor_256_avx512(v[13], v[2]);
  v[14] = xor_256_avx512(v[14], v[3]);
  v[15] = rot16_256_avx512(v[15]);
  v[12] = rot16_256_avx512(v[12]);
  v[13] = rot16_256_avx512(v[13]);
  v[14] = rot16_256_avx512(v[14]);
  v[10] = add_256_avx512(v[10], v[15]);
  v[11] = add_256_avx512(v[11], v[12]);
  v[8]  = add_256_avx512(v[8], v[13]);
  v[9]  = add_256_avx512(v[9], v[14]);
  v[5]  = xor_256_avx512(v[5], v[10]);
  v[6]  = xor_256_avx512(v[6], v[11]);
  v[7]  = xor_256_avx512(v[7], v[8]);
  v[4]  = xor_256_avx512(v[4], v[9]);
  v[5]  = rot12_256_avx512(v[5]);
  v[6]  = rot12_256_avx512(v[6]);
  v[7]  = rot12_256_avx512(v[7]);
  v[4]  = rot12_256_avx512(v[4]);
  v[0]  = add_256_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = add_256_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = add_256_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = add_256_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = add_256_avx512(v[0], v[5]);
  v[1]  = add_256_avx512(v[1], v[6]);
  v[2]  = add_256_avx512(v[2], v[7]);
  v[3]  = add_256_avx512(v[3], v[4]);
  v[15] = xor_256_avx512(v[15], v[0]);
  v[12] = xor_256_avx512(v[12], v[1]);
  v[13] = xor_256_avx512(v[13], v[2]);
  v[14] = xor_256_avx512(v[14], v[3]);
  v[15] = rot8_256_avx512(v[15]);
  v[12] = rot8_256_avx512(v[12]);
  v[13] = rot8_256_avx512(v[13]);
  v[14] = rot8_256_avx512(v[14]);
  v[10] = add_256_avx512(v[10], v[15]);
  v[11] = add_256_avx512(v[11], v[12]);
  v[8]  = add_256_avx512(v[8], v[13]);
  v[9]  = add_256_avx512(v[9], v[14]);
  v[5]  = xor_256_avx512(v[5], v[10]);
  v[6]  = xor_256_avx512(v[6], v[11]);
  v[7]  = xor_256_avx512(v[7], v[8]);
  v[4]  = xor_256_avx512(v[4], v[9]);
  v[5]  = rot7_256_avx512(v[5]);
  v[6]  = rot7_256_avx512(v[6]);
  v[7]  = rot7_256_avx512(v[7]);
  v[4]  = rot7_256_avx512(v[4]);
}

static inline void
round_fn16_avx512(__m512i v[16], __m512i m[16], size_t r)
{
  v[0]  = add_512_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][0]]);
  v[1]  = add_512_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][2]]);
  v[2]  = add_512_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][4]]);
  v[3]  = add_512_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][6]]);
  v[0]  = add_512_avx512(v[0], v[4]);
  v[1]  = add_512_avx512(v[1], v[5]);
  v[2]  = add_512_avx512(v[2], v[6]);
  v[3]  = add_512_avx512(v[3], v[7]);
  v[12] = xor_512_avx512(v[12], v[0]);
  v[13] = xor_512_avx512(v[13], v[1]);
  v[14] = xor_512_avx512(v[14], v[2]);
  v[15] = xor_512_avx512(v[15], v[3]);
  v[12] = rot16_512_avx512(v[12]);
  v[13] = rot16_512_avx512(v[13]);
  v[14] = rot16_512_avx512(v[14]);
  v[15] = rot16_512_avx512(v[15]);
  v[8]  = add_512_avx512(v[8], v[12]);
  v[9]  = add_512_avx512(v[9], v[13]);
  v[10] = add_512_avx512(v[10], v[14]);
  v[11] = add_512_avx512(v[11], v[15]);
  v[4]  = xor_512_avx512(v[4], v[8]);
  v[5]  = xor_512_avx512(v[5], v[9]);
  v[6]  = xor_512_avx512(v[6], v[10]);
  v[7]  = xor_512_avx512(v[7], v[11]);
  v[4]  = rot12_512_avx512(v[4]);
  v[5]  = rot12_512_avx512(v[5]);
  v[6]  = rot12_512_avx512(v[6]);
  v[7]  = rot12_512_avx512(v[7]);
  v[0]  = add_512_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][1]]);
  v[1]  = add_512_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][3]]);
  v[2]  = add_512_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][5]]);
  v[3]  = add_512_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][7]]);
  v[0]  = add_512_avx512(v[0], v[4]);
  v[1]  = add_512_avx512(v[1], v[5]);
  v[2]  = add_512_avx512(v[2], v[6]);
  v[3]  = add_512_avx512(v[3], v[7]);
  v[12] = xor_512_avx512(v[12], v[0]);
  v[13] = xor_512_avx512(v[13], v[1]);
  v[14] = xor_512_avx512(v[14], v[2]);
  v[15] = xor_512_avx512(v[15], v[3]);
  v[12] = rot8_512_avx512(v[12]);
  v[13] = rot8_512_avx512(v[13]);
  v[14] = rot8_512_avx512(v[14]);
  v[15] = rot8_512_avx512(v[15]);
  v[8]  = add_512_avx512(v[8], v[12]);
  v[9]  = add_512_avx512(v[9], v[13]);
  v[10] = add_512_avx512(v[10], v[14]);
  v[11] = add_512_avx512(v[11], v[15]);
  v[4]  = xor_512_avx512(v[4], v[8]);
  v[5]  = xor_512_avx512(v[5], v[9]);
  v[6]  = xor_512_avx512(v[6], v[10]);
  v[7]  = xor_512_avx512(v[7], v[11]);
  v[4]  = rot7_512_avx512(v[4]);
  v[5]  = rot7_512_avx512(v[5]);
  v[6]  = rot7_512_avx512(v[6]);
  v[7]  = rot7_512_avx512(v[7]);

  v[0]  = add_512_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][8]]);
  v[1]  = add_512_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][10]]);
  v[2]  = add_512_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][12]]);
  v[3]  = add_512_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][14]]);
  v[0]  = add_512_avx512(v[0], v[5]);
  v[1]  = add_512_avx512(v[1], v[6]);
  v[2]  = add_512_avx512(v[2], v[7]);
  v[3]  = add_512_avx512(v[3], v[4]);
  v[15] = xor_512_avx512(v[15], v[0]);
  v[12] = xor_512_avx512(v[12], v[1]);
  v[13] = xor_512_avx512(v[13], v[2]);
  v[14] = xor_512_avx512(v[14], v[3]);
  v[15] = rot16_512_avx512(v[15]);
  v[12] = rot16_512_avx512(v[12]);
  v[13] = rot16_512_avx512(v[13]);
  v[14] = rot16_512_avx512(v[14]);
  v[10] = add_512_avx512(v[10], v[15]);
  v[11] = add_512_avx512(v[11], v[12]);
  v[8]  = add_512_avx512(v[8], v[13]);
  v[9]  = add_512_avx512(v[9], v[14]);
  v[5]  = xor_512_avx512(v[5], v[10]);
  v[6]  = xor_512_avx512(v[6], v[11]);
  v[7]  = xor_512_avx512(v[7], v[8]);
  v[4]  = xor_512_avx512(v[4], v[9]);
  v[5]  = rot12_512_avx512(v[5]);
  v[6]  = rot12_512_avx512(v[6]);
  v[7]  = rot12_512_avx512(v[7]);
  v[4]  = rot12_512_avx512(v[4]);
  v[0]  = add_512_avx512(v[0], m[(size_t)MSG_SCHEDULE[r][9]]);
  v[1]  = add_512_avx512(v[1], m[(size_t)MSG_SCHEDULE[r][11]]);
  v[2]  = add_512_avx512(v[2], m[(size_t)MSG_SCHEDULE[r][13]]);
  v[3]  = add_512_avx512(v[3], m[(size_t)MSG_SCHEDULE[r][15]]);
  v[0]  = add_512_avx512(v[0], v[5]);
  v[1]  = add_512_avx512(v[1], v[6]);
  v[2]  = add_512_avx512(v[2], v[7]);
  v[3]  = add_512_avx512(v[3], v[4]);
  v[15] = xor_512_avx512(v[15], v[0]);
  v[12] = xor_512_avx512(v[12], v[1]);
  v[13] = xor_512_avx512(v[13], v[2]);
  v[14] = xor_512_avx512(v[14], v[3]);
  v[15] = rot8_512_avx512(v[15]);
  v[12] = rot8_512_avx512(v[12]);
  v[13] = rot8_512_avx512(v[13]);
  v[14] = rot8_512_avx512(v[14]);
  v[10] = add_512_avx512(v[10], v[15]);
  v[11] = add_512_avx512(v[11], v[12]);
  v[8]  = add_512_avx512(v[8], v[13]);
  v[9]  = add_512_avx512(v[9], v[14]);
  v[5]  = xor_512_avx512(v[5], v[10]);
  v[6]  = xor_512_avx512(v[6], v[11]);
  v[7]  = xor_512_avx512(v[7], v[8]);
  v[4]  = xor_512_avx512(v[4], v[9]);
  v[5]  = rot7_512_avx512(v[5]);
  v[6]  = rot7_512_avx512(v[6]);
  v[7]  = rot7_512_avx512(v[7]);
  v[4]  = rot7_512_avx512(v[4]);
}

#define LO_IMM8 0x88
#define HI_IMM8 0xdd

static inline __m512i
unpack_lo_128_avx512(__m512i a, __m512i b)
{
  return _mm512_shuffle_i32x4(a, b, LO_IMM8);
}

static inline __m512i
unpack_hi_128_avx512(__m512i a, __m512i b)
{
  return _mm512_shuffle_i32x4(a, b, HI_IMM8);
}

static inline void
transpose_vecs_512_avx512(__m512i vecs[16])
{
  __m512i ab_0 = _mm512_unpacklo_epi32(vecs[0], vecs[1]);
  __m512i ab_2 = _mm512_unpackhi_epi32(vecs[0], vecs[1]);
  __m512i cd_0 = _mm512_unpacklo_epi32(vecs[2], vecs[3]);
  __m512i cd_2 = _mm512_unpackhi_epi32(vecs[2], vecs[3]);
  __m512i ef_0 = _mm512_unpacklo_epi32(vecs[4], vecs[5]);
  __m512i ef_2 = _mm512_unpackhi_epi32(vecs[4], vecs[5]);
  __m512i gh_0 = _mm512_unpacklo_epi32(vecs[6], vecs[7]);
  __m512i gh_2 = _mm512_unpackhi_epi32(vecs[6], vecs[7]);
  __m512i ij_0 = _mm512_unpacklo_epi32(vecs[8], vecs[9]);
  __m512i ij_2 = _mm512_unpackhi_epi32(vecs[8], vecs[9]);
  __m512i kl_0 = _mm512_unpacklo_epi32(vecs[10], vecs[11]);
  __m512i kl_2 = _mm512_unpackhi_epi32(vecs[10], vecs[11]);
  __m512i mn_0 = _mm512_unpacklo_epi32(vecs[12], vecs[13]);
  __m512i mn_2 = _mm512_unpackhi_epi32(vecs[12], vecs[13]);
  __m512i op_0 = _mm512_unpacklo_epi32(vecs[14], vecs[15]);
  __m512i op_2 = _mm512_unpackhi_epi32(vecs[14], vecs[15]);

  __m512i abcd_0 = _mm512_unpacklo_epi64(ab_0, cd_0);
  __m512i abcd_1 = _mm512_unpackhi_epi64(ab_0, cd_0);
  __m512i abcd_2 = _mm512_unpacklo_epi64(ab_2, cd_2);
  __m512i abcd_3 = _mm512_unpackhi_epi64(ab_2, cd_2);
  __m512i efgh_0 = _mm512_unpacklo_epi64(ef_0, gh_0);
  __m512i efgh_1 = _mm512_unpackhi_epi64(ef_0, gh_0);
  __m512i efgh_2 = _mm512_unpacklo_epi64(ef_2, gh_2);
  __m512i efgh_3 = _mm512_unpackhi_epi64(ef_2, gh_2);
  __m512i ijkl_0 = _mm512_unpacklo_epi64(ij_0, kl_0);
  __m512i ijkl_1 = _mm512_unpackhi_epi64(ij_0, kl_0);
  __m512i ijkl_2 = _mm512_unpacklo_epi64(ij_2, kl_2);
  __m512i ijkl_3 = _mm512_unpackhi_epi64(ij_2, kl_2);
  __m512i mnop_0 = _mm512_unpacklo_epi64(mn_0, op_0);
  __m512i mnop_1 = _mm512_unpackhi_epi64(mn_0, op_0);
  __m512i mnop_2 = _mm512_unpacklo_epi64(mn_2, op_2);
  __m512i mnop_3 = _mm512_unpackhi_epi64(mn_2, op_2);

  __m512i abcdefgh_0 = unpack_lo_128_avx512(abcd_0, efgh_0);
  __m512i abcdefgh_1 = unpack_lo_128_avx512(abcd_1, efgh_1);
  __m512i abcdefgh_2 = unpack_lo_128_avx512(abcd_2, efgh_2);
  __m512i abcdefgh_3 = unpack_lo_128_avx512(abcd_3, efgh_3);
  __m512i abcdefgh_4 = unpack_hi_128_avx512(abcd_0, efgh_0);
  __m512i abcdefgh_5 = unpack_hi_128_avx512(abcd_1, efgh_1);
  __m512i abcdefgh_6 = unpack_hi_128_avx512(abcd_2, efgh_2);
  __m512i abcdefgh_7 = unpack_hi_128_avx512(abcd_3, efgh_3);
  __m512i ijklmnop_0 = unpack_lo_128_avx512(ijkl_0, mnop_0);
  __m512i ijklmnop_1 = unpack_lo_128_avx512(ijkl_1, mnop_1);
  __m512i ijklmnop_2 = unpack_lo_128_avx512(ijkl_2, mnop_2);
  __m512i ijklmnop_3 = unpack_lo_128_avx512(ijkl_3, mnop_3);
  __m512i ijklmnop_4 = unpack_hi_128_avx512(ijkl_0, mnop_0);
  __m512i ijklmnop_5 = unpack_hi_128_avx512(ijkl_1, mnop_1);
  __m512i ijklmnop_6 = unpack_hi_128_avx512(ijkl_2, mnop_2);
  __m512i ijklmnop_7 = unpack_hi_128_avx512(ijkl_3, mnop_3);

  vecs[0]  = unpack_lo_128_avx512(abcdefgh_0, ijklmnop_0);
  vecs[1]  = unpack_lo_128_avx512(abcdefgh_1, ijklmnop_1);
  vecs[2]  = unpack_lo_128_avx512(abcdefgh_2, ijklmnop_2);
  vecs[3]  = unpack_lo_128_avx512(abcdefgh_3, ijklmnop_3);
  vecs[4]  = unpack_lo_128_avx512(abcdefgh_4, ijklmnop_4);
  vecs[5]  = unpack_lo_128_avx512(abcdefgh_5, ijklmnop_5);
  vecs[6]  = unpack_lo_128_avx512(abcdefgh_6, ijklmnop_6);
  vecs[7]  = unpack_lo_128_avx512(abcdefgh_7, ijklmnop_7);
  vecs[8]  = unpack_hi_128_avx512(abcdefgh_0, ijklmnop_0);
  vecs[9]  = unpack_hi_128_avx512(abcdefgh_1, ijklmnop_1);
  vecs[10] = unpack_hi_128_avx512(abcdefgh_2, ijklmnop_2);
  vecs[11] = unpack_hi_128_avx512(abcdefgh_3, ijklmnop_3);
  vecs[12] = unpack_hi_128_avx512(abcdefgh_4, ijklmnop_4);
  vecs[13] = unpack_hi_128_avx512(abcdefgh_5, ijklmnop_5);
  vecs[14] = unpack_hi_128_avx512(abcdefgh_6, ijklmnop_6);
  vecs[15] = unpack_hi_128_avx512(abcdefgh_7, ijklmnop_7);
}

static inline void
transpose_msg_vecs16_avx512(const uint8_t *const *inputs, size_t block_offset, __m512i out_msg[16])
{
  size_t i;

  out_msg[0]  = loadu_512_avx512(&inputs[0][block_offset]);
  out_msg[1]  = loadu_512_avx512(&inputs[1][block_offset]);
  out_msg[2]  = loadu_512_avx512(&inputs[2][block_offset]);
  out_msg[3]  = loadu_512_avx512(&inputs[3][block_offset]);
  out_msg[4]  = loadu_512_avx512(&inputs[4][block_offset]);
  out_msg[5]  = loadu_512_avx512(&inputs[5][block_offset]);
  out_msg[6]  = loadu_512_avx512(&inputs[6][block_offset]);
  out_msg[7]  = loadu_512_avx512(&inputs[7][block_offset]);
  out_msg[8]  = loadu_512_avx512(&inputs[8][block_offset]);
  out_msg[9]  = loadu_512_avx512(&inputs[9][block_offset]);
  out_msg[10] = loadu_512_avx512(&inputs[10][block_offset]);
  out_msg[11] = loadu_512_avx512(&inputs[11][block_offset]);
  out_msg[12] = loadu_512_avx512(&inputs[12][block_offset]);
  out_msg[13] = loadu_512_avx512(&inputs[13][block_offset]);
  out_msg[14] = loadu_512_avx512(&inputs[14][block_offset]);
  out_msg[15] = loadu_512_avx512(&inputs[15][block_offset]);

  for (i = 0; i < 16; i++) {
    _mm_prefetch((const void *)&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs_512_avx512(out_msg);
}

static inline void
load_counters16_avx512(uint64_t counter, int increment_counter, __m512i *out_lo, __m512i *out_hi)
{
  const __m512i mask   = _mm512_set1_epi32(-increment_counter);
  const __m512i deltas = _mm512_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
  const __m512i masked_deltas = _mm512_and_si512(deltas, mask);
  const __m512i low_words = _mm512_add_epi32(_mm512_set1_epi32((int32_t)counter), masked_deltas);
  const __m512i carries =
      _mm512_srli_epi32(_mm512_andnot_si512(low_words, _mm512_set1_epi32((int32_t)counter)), 31);
  const __m512i high_words = _mm512_add_epi32(_mm512_set1_epi32((int32_t)(counter >> 32)), carries);

  *out_lo = low_words;
  *out_hi = high_words;
}

void
blake3_hash16_avx512(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m512i h_vecs[8];
  __m512i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_512_avx512(key[0]);
  h_vecs[1] = set1_512_avx512(key[1]);
  h_vecs[2] = set1_512_avx512(key[2]);
  h_vecs[3] = set1_512_avx512(key[3]);
  h_vecs[4] = set1_512_avx512(key[4]);
  h_vecs[5] = set1_512_avx512(key[5]);
  h_vecs[6] = set1_512_avx512(key[6]);
  h_vecs[7] = set1_512_avx512(key[7]);

  load_counters16_avx512(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m512i block_len_vec;
    __m512i block_flags_vec;
    __m512i msg_vecs[16];
    __m512i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_512_avx512(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_512_avx512(block_flags);
    transpose_msg_vecs16_avx512(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_512_avx512(IV[0]);
    v[9]  = set1_512_avx512(IV[1]);
    v[10] = set1_512_avx512(IV[2]);
    v[11] = set1_512_avx512(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn16_avx512(v, msg_vecs, 0);
    round_fn16_avx512(v, msg_vecs, 1);
    round_fn16_avx512(v, msg_vecs, 2);
    round_fn16_avx512(v, msg_vecs, 3);
    round_fn16_avx512(v, msg_vecs, 4);
    round_fn16_avx512(v, msg_vecs, 5);
    round_fn16_avx512(v, msg_vecs, 6);

    h_vecs[0] = xor_512_avx512(v[0], v[8]);
    h_vecs[1] = xor_512_avx512(v[1], v[9]);
    h_vecs[2] = xor_512_avx512(v[2], v[10]);
    h_vecs[3] = xor_512_avx512(v[3], v[11]);
    h_vecs[4] = xor_512_avx512(v[4], v[12]);
    h_vecs[5] = xor_512_avx512(v[5], v[13]);
    h_vecs[6] = xor_512_avx512(v[6], v[14]);
    h_vecs[7] = xor_512_avx512(v[7], v[15]);

    block_flags = flags;
  }

  __m512i padded[16] = {
      h_vecs[0],
      h_vecs[1],
      h_vecs[2],
      h_vecs[3],
      h_vecs[4],
      h_vecs[5],
      h_vecs[6],
      h_vecs[7],
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
      set1_512_avx512(0),
  };
  transpose_vecs_512_avx512(padded);
  _mm256_mask_storeu_epi32(
      &out_bytes[0 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[0])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[1 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[1])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[2 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[2])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[3 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[3])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[4 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[4])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[5 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[5])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[6 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[6])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[7 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[7])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[8 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[8])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[9 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[9])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[10 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[10])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[11 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[11])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[12 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[12])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[13 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[13])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[14 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[14])
  );
  _mm256_mask_storeu_epi32(
      &out_bytes[15 * sizeof(__m256i)], (__mmask8)-1, _mm512_castsi512_si256(padded[15])
  );
}

static inline void
transpose_msg_vecs8_avx512(const uint8_t *const *inputs, size_t block_offset, __m256i out_msg[16])
{
  out_msg[0]  = loadu_256_avx512(&inputs[0][block_offset]);
  out_msg[1]  = loadu_256_avx512(&inputs[1][block_offset]);
  out_msg[2]  = loadu_256_avx512(&inputs[2][block_offset]);
  out_msg[3]  = loadu_256_avx512(&inputs[3][block_offset]);
  out_msg[4]  = loadu_256_avx512(&inputs[4][block_offset]);
  out_msg[5]  = loadu_256_avx512(&inputs[5][block_offset]);
  out_msg[6]  = loadu_256_avx512(&inputs[6][block_offset]);
  out_msg[7]  = loadu_256_avx512(&inputs[7][block_offset]);
  out_msg[8]  = loadu_256_avx512(&inputs[0][block_offset + 32]);
  out_msg[9]  = loadu_256_avx512(&inputs[1][block_offset + 32]);
  out_msg[10] = loadu_256_avx512(&inputs[2][block_offset + 32]);
  out_msg[11] = loadu_256_avx512(&inputs[3][block_offset + 32]);
  out_msg[12] = loadu_256_avx512(&inputs[4][block_offset + 32]);
  out_msg[13] = loadu_256_avx512(&inputs[5][block_offset + 32]);
  out_msg[14] = loadu_256_avx512(&inputs[6][block_offset + 32]);
  out_msg[15] = loadu_256_avx512(&inputs[7][block_offset + 32]);

  transpose_vecs_avx2(out_msg);
}

static inline void
load_counters8_avx512(uint64_t counter, int increment_counter, __m256i *out_lo, __m256i *out_hi)
{
  const __m256i mask = _mm256_set1_epi32(-increment_counter);
  const __m256i add0 = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
  const __m256i add1 = _mm256_and_si256(mask, add0);
  __m256i       l    = _mm256_add_epi32(_mm256_set1_epi32((int32_t)counter), add1);
  __m256i       carry =
      _mm256_srli_epi32(_mm256_andnot_si256(l, _mm256_set1_epi32((int32_t)counter)), 31);
  __m256i h = _mm256_add_epi32(_mm256_set1_epi32((int32_t)(counter >> 32)), carry);

  *out_lo = l;
  *out_hi = h;
}

void
blake3_hash8_avx512(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m256i h_vecs[8];
  __m256i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_256_avx512(key[0]);
  h_vecs[1] = set1_256_avx512(key[1]);
  h_vecs[2] = set1_256_avx512(key[2]);
  h_vecs[3] = set1_256_avx512(key[3]);
  h_vecs[4] = set1_256_avx512(key[4]);
  h_vecs[5] = set1_256_avx512(key[5]);
  h_vecs[6] = set1_256_avx512(key[6]);
  h_vecs[7] = set1_256_avx512(key[7]);

  load_counters8_avx512(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m256i block_len_vec;
    __m256i block_flags_vec;
    __m256i msg_vecs[16];
    __m256i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_256_avx512(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_256_avx512(block_flags);
    transpose_msg_vecs8_avx512(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_256_avx512(IV[0]);
    v[9]  = set1_256_avx512(IV[1]);
    v[10] = set1_256_avx512(IV[2]);
    v[11] = set1_256_avx512(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn8_avx512(v, msg_vecs, 0);
    round_fn8_avx512(v, msg_vecs, 1);
    round_fn8_avx512(v, msg_vecs, 2);
    round_fn8_avx512(v, msg_vecs, 3);
    round_fn8_avx512(v, msg_vecs, 4);
    round_fn8_avx512(v, msg_vecs, 5);
    round_fn8_avx512(v, msg_vecs, 6);

    h_vecs[0] = xor_256_avx512(v[0], v[8]);
    h_vecs[1] = xor_256_avx512(v[1], v[9]);
    h_vecs[2] = xor_256_avx512(v[2], v[10]);
    h_vecs[3] = xor_256_avx512(v[3], v[11]);
    h_vecs[4] = xor_256_avx512(v[4], v[12]);
    h_vecs[5] = xor_256_avx512(v[5], v[13]);
    h_vecs[6] = xor_256_avx512(v[6], v[14]);
    h_vecs[7] = xor_256_avx512(v[7], v[15]);

    block_flags = flags;
  }

  transpose_vecs_avx2(h_vecs);
  storeu_256_avx512(h_vecs[0], &out_bytes[0 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[1], &out_bytes[1 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[2], &out_bytes[2 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[3], &out_bytes[3 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[4], &out_bytes[4 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[5], &out_bytes[5 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[6], &out_bytes[6 * sizeof(__m256i)]);
  storeu_256_avx512(h_vecs[7], &out_bytes[7 * sizeof(__m256i)]);
}

static inline void
transpose_msg_vecs4_avx512(const uint8_t *const *inputs, size_t block_offset, __m128i out_msg[16])
{
  out_msg[0]  = loadu_128_avx512(&inputs[0][block_offset]);
  out_msg[1]  = loadu_128_avx512(&inputs[1][block_offset]);
  out_msg[2]  = loadu_128_avx512(&inputs[2][block_offset]);
  out_msg[3]  = loadu_128_avx512(&inputs[3][block_offset]);
  out_msg[4]  = loadu_128_avx512(&inputs[0][block_offset + 16]);
  out_msg[5]  = loadu_128_avx512(&inputs[1][block_offset + 16]);
  out_msg[6]  = loadu_128_avx512(&inputs[2][block_offset + 16]);
  out_msg[7]  = loadu_128_avx512(&inputs[3][block_offset + 16]);
  out_msg[8]  = loadu_128_avx512(&inputs[0][block_offset + 32]);
  out_msg[9]  = loadu_128_avx512(&inputs[1][block_offset + 32]);
  out_msg[10] = loadu_128_avx512(&inputs[2][block_offset + 32]);
  out_msg[11] = loadu_128_avx512(&inputs[3][block_offset + 32]);
  out_msg[12] = loadu_128_avx512(&inputs[0][block_offset + 48]);
  out_msg[13] = loadu_128_avx512(&inputs[1][block_offset + 48]);
  out_msg[14] = loadu_128_avx512(&inputs[2][block_offset + 48]);
  out_msg[15] = loadu_128_avx512(&inputs[3][block_offset + 48]);

  transpose_vecs_sse2(out_msg);
}

static inline void
load_counters4_avx512(uint64_t counter, int increment_counter, __m128i *out_lo, __m128i *out_hi)
{
  const __m128i mask  = _mm_set1_epi32(-increment_counter);
  const __m128i add0  = _mm_set_epi32(3, 2, 1, 0);
  const __m128i add1  = _mm_and_si128(mask, add0);
  __m128i       l     = _mm_add_epi32(_mm_set1_epi32((int32_t)counter), add1);
  __m128i       carry = _mm_srli_epi32(_mm_andnot_si128(l, _mm_set1_epi32((int32_t)counter)), 31);
  __m128i       h     = _mm_add_epi32(_mm_set1_epi32((int32_t)(counter >> 32)), carry);

  *out_lo = l;
  *out_hi = h;
}

void
blake3_hash4_avx512(
    const uint8_t *const *inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  __m128i h_vecs[8];
  __m128i counter_low_vec, counter_high_vec;
  uint8_t block_flags;
  size_t  block;

  h_vecs[0] = set1_128_avx512(key[0]);
  h_vecs[1] = set1_128_avx512(key[1]);
  h_vecs[2] = set1_128_avx512(key[2]);
  h_vecs[3] = set1_128_avx512(key[3]);
  h_vecs[4] = set1_128_avx512(key[4]);
  h_vecs[5] = set1_128_avx512(key[5]);
  h_vecs[6] = set1_128_avx512(key[6]);
  h_vecs[7] = set1_128_avx512(key[7]);

  load_counters4_avx512(counter, increment_counter, &counter_low_vec, &counter_high_vec);
  block_flags = flags | flags_start;

  for (block = 0; block < blocks; block++) {
    __m128i block_len_vec;
    __m128i block_flags_vec;
    __m128i msg_vecs[16];
    __m128i v[16];

    if (block + 1 == blocks) {
      block_flags |= flags_end;
    }
    block_len_vec   = set1_128_avx512(BLAKE3_BLOCK_LEN);
    block_flags_vec = set1_128_avx512(block_flags);
    transpose_msg_vecs4_avx512(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    v[0]  = h_vecs[0];
    v[1]  = h_vecs[1];
    v[2]  = h_vecs[2];
    v[3]  = h_vecs[3];
    v[4]  = h_vecs[4];
    v[5]  = h_vecs[5];
    v[6]  = h_vecs[6];
    v[7]  = h_vecs[7];
    v[8]  = set1_128_avx512(IV[0]);
    v[9]  = set1_128_avx512(IV[1]);
    v[10] = set1_128_avx512(IV[2]);
    v[11] = set1_128_avx512(IV[3]);
    v[12] = counter_low_vec;
    v[13] = counter_high_vec;
    v[14] = block_len_vec;
    v[15] = block_flags_vec;

    round_fn4_avx512(v, msg_vecs, 0);
    round_fn4_avx512(v, msg_vecs, 1);
    round_fn4_avx512(v, msg_vecs, 2);
    round_fn4_avx512(v, msg_vecs, 3);
    round_fn4_avx512(v, msg_vecs, 4);
    round_fn4_avx512(v, msg_vecs, 5);
    round_fn4_avx512(v, msg_vecs, 6);

    h_vecs[0] = xor_128_avx512(v[0], v[8]);
    h_vecs[1] = xor_128_avx512(v[1], v[9]);
    h_vecs[2] = xor_128_avx512(v[2], v[10]);
    h_vecs[3] = xor_128_avx512(v[3], v[11]);
    h_vecs[4] = xor_128_avx512(v[4], v[12]);
    h_vecs[5] = xor_128_avx512(v[5], v[13]);
    h_vecs[6] = xor_128_avx512(v[6], v[14]);
    h_vecs[7] = xor_128_avx512(v[7], v[15]);

    block_flags = flags;
  }

  transpose_vecs_sse2(h_vecs);
  storeu_128_avx512(h_vecs[0], &out_bytes[0 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[1], &out_bytes[2 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[2], &out_bytes[4 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[3], &out_bytes[6 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[4], &out_bytes[1 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[5], &out_bytes[3 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[6], &out_bytes[5 * sizeof(__m128i)]);
  storeu_128_avx512(h_vecs[7], &out_bytes[7 * sizeof(__m128i)]);
}

static inline void
hash_one_avx512(
    const uint8_t *input,
    size_t         blocks,
    const uint32_t key[8],
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        flags_start,
    uint8_t        flags_end,
    uint8_t        out_bytes[BLAKE3_OUT_LEN]
)
{
  uint32_t cv[8];
  uint8_t  block_flags;

  memcpy(cv, key, BLAKE3_KEY_LEN);
  block_flags = flags | flags_start;
  while (blocks > 0) {
    if (blocks == 1) {
      block_flags |= flags_end;
    }
    blake3_compress_in_place_avx512(cv, input, BLAKE3_BLOCK_LEN, counter, block_flags);
    input = &input[BLAKE3_BLOCK_LEN];
    blocks -= 1;
    block_flags = flags;
  }
  memcpy(out_bytes, cv, BLAKE3_OUT_LEN);
}

void
blake3_hash_many_avx512(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out_bytes
)
{
  while (num_inputs >= 16) {
    blake3_hash16_avx512(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += 16;
    }
    inputs += 16;
    num_inputs -= 16;
    out_bytes = &out_bytes[16 * BLAKE3_OUT_LEN];
  }
  while (num_inputs >= 8) {
    blake3_hash8_avx512(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += 8;
    }
    inputs += 8;
    num_inputs -= 8;
    out_bytes = &out_bytes[8 * BLAKE3_OUT_LEN];
  }
  while (num_inputs >= 4) {
    blake3_hash4_avx512(
        inputs, blocks, key, counter, increment_counter, flags, flags_start, flags_end, out_bytes
    );
    if (increment_counter) {
      counter += 4;
    }
    inputs += 4;
    num_inputs -= 4;
    out_bytes = &out_bytes[4 * BLAKE3_OUT_LEN];
  }
  while (num_inputs > 0) {
    hash_one_avx512(inputs[0], blocks, key, counter, flags, flags_start, flags_end, out_bytes);
    if (increment_counter) {
      counter += 1;
    }
    inputs += 1;
    num_inputs -= 1;
    out_bytes = &out_bytes[BLAKE3_OUT_LEN];
  }
}
#if defined(__clang__)
#pragma clang attribute pop
#elif defined(__GNUC__)
#pragma GCC pop_options
#endif
#endif

/* dispatch functions */
void
blake3_compress_in_place(
    uint32_t      cv[8],
    const uint8_t block[BLAKE3_BLOCK_LEN],
    uint8_t       block_len,
    uint64_t      counter,
    uint8_t       flags
)
{
#if BLAKE3_X86_SIMD
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
  if (blake3_cpu_features & AVX512) {
    blake3_compress_in_place_avx512(cv, block, block_len, counter, flags);
    return;
  }
  if (blake3_cpu_features & SSE41) {
    blake3_compress_in_place_sse41(cv, block, block_len, counter, flags);
    return;
  }
  if (blake3_cpu_features & SSE2) {
    blake3_compress_in_place_sse2(cv, block, block_len, counter, flags);
    return;
  }
#endif
  blake3_compress_in_place_portable(cv, block, block_len, counter, flags);
}

void
blake3_compress_xof(
    const uint32_t cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags,
    uint8_t        out[64]
)
{
#if BLAKE3_X86_SIMD
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
  if (blake3_cpu_features & AVX512) {
    blake3_compress_xof_avx512(cv, block, block_len, counter, flags, out);
    return;
  }
  if (blake3_cpu_features & SSE41) {
    blake3_compress_xof_sse41(cv, block, block_len, counter, flags, out);
    return;
  }
  if (blake3_cpu_features & SSE2) {
    blake3_compress_xof_sse2(cv, block, block_len, counter, flags, out);
    return;
  }
#endif
  blake3_compress_xof_portable(cv, block, block_len, counter, flags, out);
}

void
blake3_hash_many(
    const uint8_t *const *inputs,
    size_t                num_inputs,
    size_t                blocks,
    const uint32_t        key[8],
    uint64_t              counter,
    int                   increment_counter,
    uint8_t               flags,
    uint8_t               flags_start,
    uint8_t               flags_end,
    uint8_t              *out
)
{
#if BLAKE3_X86_SIMD
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
  if (blake3_cpu_features & AVX512) {
    blake3_hash_many_avx512(
        inputs,
        num_inputs,
        blocks,
        key,
        counter,
        increment_counter,
        flags,
        flags_start,
        flags_end,
        out
    );
    return;
  }
  if (blake3_cpu_features & AVX2) {
    blake3_hash_many_avx2(
        inputs,
        num_inputs,
        blocks,
        key,
        counter,
        increment_counter,
        flags,
        flags_start,
        flags_end,
        out
    );
    return;
  }
  if (blake3_cpu_features & SSE41) {
    blake3_hash_many_sse41(
        inputs,
        num_inputs,
        blocks,
        key,
        counter,
        increment_counter,
        flags,
        flags_start,
        flags_end,
        out
    );
    return;
  }
  if (blake3_cpu_features & SSE2) {
    blake3_hash_many_sse2(
        inputs,
        num_inputs,
        blocks,
        key,
        counter,
        increment_counter,
        flags,
        flags_start,
        flags_end,
        out
    );
    return;
  }
#endif
  blake3_hash_many_portable(
      inputs,
      num_inputs,
      blocks,
      key,
      counter,
      increment_counter,
      flags,
      flags_start,
      flags_end,
      out
  );
}

size_t
blake3_simd_degree(void)
{
#if BLAKE3_X86_SIMD
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
  if (blake3_cpu_features & AVX512)
    return 16;
  if (blake3_cpu_features & AVX2)
    return 8;
  if (blake3_cpu_features & SSE41)
    return 4;
  if (blake3_cpu_features & SSE2)
    return 4;
#endif
  return 1;
}

/* core hasher implementation */
const char *
blake3_version(void)
{
  return BLAKE3_VERSION_STRING;
}

static inline void
chunk_state_init(struct Blake3ChunkState *self, const uint32_t key[8], uint8_t flags)
{
  memcpy(self->cv, key, BLAKE3_KEY_LEN);
  self->chunk_counter = 0;
  memset(self->buf, 0, BLAKE3_BLOCK_LEN);
  self->buf_len           = 0;
  self->blocks_compressed = 0;
  self->flags             = flags;
}

static inline void
chunk_state_reset(struct Blake3ChunkState *self, const uint32_t key[8], uint64_t chunk_counter)
{
  memcpy(self->cv, key, BLAKE3_KEY_LEN);
  self->chunk_counter     = chunk_counter;
  self->blocks_compressed = 0;
  memset(self->buf, 0, BLAKE3_BLOCK_LEN);
  self->buf_len = 0;
}

static inline size_t
chunk_state_len(const struct Blake3ChunkState *self)
{
  return (BLAKE3_BLOCK_LEN * (size_t)self->blocks_compressed) + ((size_t)self->buf_len);
}

static inline size_t
chunk_state_fill_buf(struct Blake3ChunkState *self, const uint8_t *input, size_t input_len)
{
  size_t   take = BLAKE3_BLOCK_LEN - ((size_t)self->buf_len);
  uint8_t *dest;

  if (take > input_len) {
    take = input_len;
  }
  dest = self->buf + ((size_t)self->buf_len);
  memcpy(dest, input, take);
  self->buf_len += (uint8_t)take;
  return take;
}

static inline uint8_t
chunk_state_maybe_start_flag(const struct Blake3ChunkState *self)
{
  if (self->blocks_compressed == 0) {
    return CHUNK_START;
  } else {
    return 0;
  }
}

static inline struct Output
make_output(
    const uint32_t input_cv[8],
    const uint8_t  block[BLAKE3_BLOCK_LEN],
    uint8_t        block_len,
    uint64_t       counter,
    uint8_t        flags
)
{
  struct Output ret;

  memcpy(ret.input_cv, input_cv, 32);
  memcpy(ret.block, block, BLAKE3_BLOCK_LEN);
  ret.block_len = block_len;
  ret.counter   = counter;
  ret.flags     = flags;
  return ret;
}

static inline void
output_chaining_value(const struct Output *self, uint8_t cv[32])
{
  uint32_t cv_words[8];

  memcpy(cv_words, self->input_cv, 32);
  blake3_compress_in_place(cv_words, self->block, self->block_len, self->counter, self->flags);
  store_cv_words(cv, cv_words);
}

static inline void
output_root_bytes(const struct Output *self, uint64_t seek, uint8_t *out, size_t out_len)
{
  uint64_t output_block_counter = seek / 64;
  size_t   offset_within_block  = seek % 64;
  uint8_t  wide_buf[64];
  size_t   available_bytes;
  size_t   memcpy_len;

  while (out_len > 0) {
    blake3_compress_xof(
        self->input_cv,
        self->block,
        self->block_len,
        output_block_counter,
        self->flags | ROOT,
        wide_buf
    );
    available_bytes = 64 - offset_within_block;
    if (out_len > available_bytes) {
      memcpy_len = available_bytes;
    } else {
      memcpy_len = out_len;
    }
    memcpy(out, wide_buf + offset_within_block, memcpy_len);
    out += memcpy_len;
    out_len -= memcpy_len;
    output_block_counter += 1;
    offset_within_block = 0;
  }
}

static inline void
chunk_state_update(struct Blake3ChunkState *self, const uint8_t *input, size_t input_len)
{
  size_t take;

  if (self->buf_len > 0) {
    take = chunk_state_fill_buf(self, input, input_len);
    input += take;
    input_len -= take;
    if (input_len > 0) {
      blake3_compress_in_place(
          self->cv,
          self->buf,
          BLAKE3_BLOCK_LEN,
          self->chunk_counter,
          self->flags | chunk_state_maybe_start_flag(self)
      );
      self->blocks_compressed += 1;
      self->buf_len = 0;
      memset(self->buf, 0, BLAKE3_BLOCK_LEN);
    }
  }

  while (input_len > BLAKE3_BLOCK_LEN) {
    blake3_compress_in_place(
        self->cv,
        input,
        BLAKE3_BLOCK_LEN,
        self->chunk_counter,
        self->flags | chunk_state_maybe_start_flag(self)
    );
    self->blocks_compressed += 1;
    input += BLAKE3_BLOCK_LEN;
    input_len -= BLAKE3_BLOCK_LEN;
  }

  take = chunk_state_fill_buf(self, input, input_len);
  input += take;
  input_len -= take;
}

static inline struct Output
chunk_state_output(const struct Blake3ChunkState *self)
{
  uint8_t block_flags = self->flags | chunk_state_maybe_start_flag(self) | CHUNK_END;

  return make_output(self->cv, self->buf, self->buf_len, self->chunk_counter, block_flags);
}

static inline struct Output
parent_output(const uint8_t block[BLAKE3_BLOCK_LEN], const uint32_t key[8], uint8_t flags)
{
  return make_output(key, block, BLAKE3_BLOCK_LEN, 0, flags | PARENT);
}

static unsigned int
highest_one(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
  return 63 ^ __builtin_clzll(x);
#else
  unsigned int c = 0;
  if (x & 0xffffffff00000000ULL) {
    x >>= 32;
    c += 32;
  }
  if (x & 0x00000000ffff0000ULL) {
    x >>= 16;
    c += 16;
  }
  if (x & 0x000000000000ff00ULL) {
    x >>= 8;
    c += 8;
  }
  if (x & 0x00000000000000f0ULL) {
    x >>= 4;
    c += 4;
  }
  if (x & 0x000000000000000cULL) {
    x >>= 2;
    c += 2;
  }
  if (x & 0x0000000000000002ULL) {
    c += 1;
  }
  return c;
#endif
}

static inline uint64_t
round_down_to_power_of_2(uint64_t x)
{
  return 1ULL << highest_one(x | 1);
}

static inline size_t
left_len(size_t content_len)
{
  size_t full_chunks = (content_len - 1) / BLAKE3_CHUNK_LEN;

  return round_down_to_power_of_2(full_chunks) * BLAKE3_CHUNK_LEN;
}

static inline size_t
compress_chunks_parallel(
    const uint8_t *input,
    size_t         input_len,
    const uint32_t key[8],
    uint64_t       chunk_counter,
    uint8_t        flags,
    uint8_t       *out
)
{
  const uint8_t *chunks_array[MAX_SIMD_DEGREE];
  size_t         input_position   = 0;
  size_t         chunks_array_len = 0;

  assert(0 < input_len);
  assert(input_len <= MAX_SIMD_DEGREE * BLAKE3_CHUNK_LEN);

  while (input_len - input_position >= BLAKE3_CHUNK_LEN) {
    chunks_array[chunks_array_len] = &input[input_position];
    input_position += BLAKE3_CHUNK_LEN;
    chunks_array_len += 1;
  }

  blake3_hash_many(
      chunks_array,
      chunks_array_len,
      BLAKE3_CHUNK_LEN / BLAKE3_BLOCK_LEN,
      key,
      chunk_counter,
      1,
      flags,
      CHUNK_START,
      CHUNK_END,
      out
  );

  if (input_len > input_position) {
    uint64_t                counter = chunk_counter + (uint64_t)chunks_array_len;
    struct Blake3ChunkState chunk_state;
    struct Output           output;

    chunk_state_init(&chunk_state, key, flags);
    chunk_state.chunk_counter = counter;
    chunk_state_update(&chunk_state, &input[input_position], input_len - input_position);
    output = chunk_state_output(&chunk_state);
    output_chaining_value(&output, &out[chunks_array_len * BLAKE3_OUT_LEN]);
    return chunks_array_len + 1;
  } else {
    return chunks_array_len;
  }
}

static inline size_t
compress_parents_parallel(
    const uint8_t *child_chaining_values,
    size_t         num_chaining_values,
    const uint32_t key[8],
    uint8_t        flags,
    uint8_t       *out
)
{
  const uint8_t *parents_array[MAX_SIMD_DEGREE_OR_2];
  size_t         parents_array_len = 0;

  assert(2 <= num_chaining_values);
  assert(num_chaining_values <= 2 * MAX_SIMD_DEGREE_OR_2);

  while (num_chaining_values - (2 * parents_array_len) >= 2) {
    parents_array[parents_array_len] =
        &child_chaining_values[2 * parents_array_len * BLAKE3_OUT_LEN];
    parents_array_len += 1;
  }

  blake3_hash_many(parents_array, parents_array_len, 1, key, 0, 0, flags | PARENT, 0, 0, out);

  if (num_chaining_values > 2 * parents_array_len) {
    memcpy(
        &out[parents_array_len * BLAKE3_OUT_LEN],
        &child_chaining_values[2 * parents_array_len * BLAKE3_OUT_LEN],
        BLAKE3_OUT_LEN
    );
    return parents_array_len + 1;
  } else {
    return parents_array_len;
  }
}

/* splits input in two at the largest power-of-2 chunk boundary and
 * recurses on each half independently, writing each side's chaining
 * values into its own slice of out. this, not an n-way iterate-then-
 * merge, is what actually keeps the chaining-value count bounded at
 * every level: an earlier iterative reconstruction of this function
 * let cvs_written grow to roughly degree^2 for exact power-of-2 input
 * lengths (64kib was the first size it broke on), overrunning the
 * bound compress_parents_parallel assumes. fixed by matching the
 * upstream reference implementation's actual recursive shape instead
 * of re-deriving it */
static inline size_t
blake3_compress_subtree_wide(
    const uint8_t *input,
    size_t         input_len,
    const uint32_t key[8],
    uint64_t       chunk_counter,
    uint8_t        flags,
    uint8_t       *out
)
{
  size_t         left_input_len, right_input_len;
  const uint8_t *right_input;
  uint64_t       right_chunk_counter;
  uint8_t        cv_array[2 * MAX_SIMD_DEGREE_OR_2 * BLAKE3_OUT_LEN];
  size_t         degree;
  uint8_t       *right_cvs;
  size_t         left_n, right_n;

  if (input_len <= (size_t)blake3_simd_degree() * BLAKE3_CHUNK_LEN) {
    return compress_chunks_parallel(input, input_len, key, chunk_counter, flags, out);
  }

  left_input_len = left_len(input_len);
  right_input_len = input_len - left_input_len;
  right_input = &input[left_input_len];
  right_chunk_counter = chunk_counter + (uint64_t)(left_input_len / BLAKE3_CHUNK_LEN);

  /* MAX_SIMD_DEGREE_OR_2 accounts for the degree=1 special case below,
 * which always returns 2 outputs even though degree itself is 1 */
  degree = blake3_simd_degree();
  if (left_input_len > BLAKE3_CHUNK_LEN && degree == 1) {
    degree = 2;
  }
  right_cvs = &cv_array[degree * BLAKE3_OUT_LEN];

  left_n =
      blake3_compress_subtree_wide(input, left_input_len, key, chunk_counter, flags, cv_array);
  right_n = blake3_compress_subtree_wide(
      right_input, right_input_len, key, right_chunk_counter, flags, right_cvs
  );

  /* degree=1: left_n and right_n are both 1, return both cvs directly
 * rather than compressing them, so the caller always sees at least 2 */
  if (left_n == 1) {
    memcpy(out, cv_array, 2 * BLAKE3_OUT_LEN);
    return 2;
  }

  return compress_parents_parallel(cv_array, left_n + right_n, key, flags, out);
}

static void
compress_subtree_to_parent_node(
    const uint8_t *input,
    size_t         input_len,
    const uint32_t key[8],
    uint64_t       chunk_counter,
    uint8_t        flags,
    uint8_t        out[2 * BLAKE3_OUT_LEN]
)
{
  uint8_t cv_array[2 * MAX_SIMD_DEGREE_OR_2 * BLAKE3_OUT_LEN];
  size_t  num_cvs =
      blake3_compress_subtree_wide(input, input_len, key, chunk_counter, flags, cv_array);

  assert(num_cvs >= 2);
  assert(num_cvs <= MAX_SIMD_DEGREE_OR_2);
  while (num_cvs > 2) {
    num_cvs = compress_parents_parallel(cv_array, num_cvs, key, flags, cv_array);
  }
  memcpy(out, cv_array, 2 * BLAKE3_OUT_LEN);
}

static inline void
hasher_init_base(struct Blake3Hasher *self, const uint32_t key[8], uint8_t flags)
{
  memcpy(self->key, key, BLAKE3_KEY_LEN);
  chunk_state_init(&self->chunk, key, flags);
  self->cv_stack_len = 0;
}

void
blake3_hasher_init(struct Blake3Hasher *self)
{
  if (!blake3_cpu_detected)
    blake3_detect_cpu_features();
  hasher_init_base(self, IV, 0);
}

static inline void
load_key_words(const uint8_t key[BLAKE3_KEY_LEN], uint32_t key_words[8])
{
  key_words[0] = load32(&key[0 * 4]);
  key_words[1] = load32(&key[1 * 4]);
  key_words[2] = load32(&key[2 * 4]);
  key_words[3] = load32(&key[3 * 4]);
  key_words[4] = load32(&key[4 * 4]);
  key_words[5] = load32(&key[5 * 4]);
  key_words[6] = load32(&key[6 * 4]);
  key_words[7] = load32(&key[7 * 4]);
}

void
blake3_hasher_init_keyed(struct Blake3Hasher *self, const uint8_t key[BLAKE3_KEY_LEN])
{
  uint32_t key_words[8];

  load_key_words(key, key_words);
  hasher_init_base(self, key_words, KEYED_HASH);
}

void
blake3_hasher_init_derive_key_raw(
    struct Blake3Hasher *self, const void *context, size_t context_len
)
{
  struct Blake3Hasher context_hasher;
  uint8_t             context_key[BLAKE3_KEY_LEN];
  uint32_t            context_key_words[8];

  hasher_init_base(&context_hasher, IV, DERIVE_KEY_CONTEXT);
  blake3_hasher_update(&context_hasher, context, context_len);
  blake3_hasher_finalize(&context_hasher, context_key, BLAKE3_KEY_LEN);
  load_key_words(context_key, context_key_words);
  hasher_init_base(self, context_key_words, DERIVE_KEY_MATERIAL);
}

static inline unsigned int
popcnt(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#else
  unsigned int count = 0;
  while (x != 0) {
    count += 1;
    x &= x - 1;
  }
  return count;
#endif
}

void
blake3_hasher_init_derive_key(struct Blake3Hasher *self, const char *context)
{
  blake3_hasher_init_derive_key_raw(self, context, strlen(context));
}

static inline void
hasher_merge_cv_stack(struct Blake3Hasher *self, uint64_t total_len)
{
  size_t post_merge_stack_len = (size_t)popcnt(total_len);

  while (self->cv_stack_len > post_merge_stack_len) {
    uint8_t      *parent_node = &self->cv_stack[(self->cv_stack_len - 2) * BLAKE3_OUT_LEN];
    struct Output output      = parent_output(parent_node, self->key, self->chunk.flags);

    output_chaining_value(&output, parent_node);
    self->cv_stack_len -= 1;
  }
}

static inline void
hasher_push_cv(struct Blake3Hasher *self, uint8_t new_cv[BLAKE3_OUT_LEN], uint64_t chunk_counter)
{
  hasher_merge_cv_stack(self, chunk_counter);
  memcpy(&self->cv_stack[self->cv_stack_len * BLAKE3_OUT_LEN], new_cv, BLAKE3_OUT_LEN);
  self->cv_stack_len += 1;
}

void
blake3_hasher_update(struct Blake3Hasher *self, const void *input, size_t input_len)
{
  const uint8_t *input_bytes;

  if (input_len == 0) {
    return;
  }

  input_bytes = (const uint8_t *)input;

  if (chunk_state_len(&self->chunk) > 0) {
    size_t take = BLAKE3_CHUNK_LEN - chunk_state_len(&self->chunk);

    if (take > input_len) {
      take = input_len;
    }
    chunk_state_update(&self->chunk, input_bytes, take);
    input_bytes += take;
    input_len -= take;
    if (input_len > 0) {
      struct Output output = chunk_state_output(&self->chunk);
      uint8_t       chunk_cv[32];

      output_chaining_value(&output, chunk_cv);
      hasher_push_cv(self, chunk_cv, self->chunk.chunk_counter);
      chunk_state_reset(&self->chunk, self->key, self->chunk.chunk_counter + 1);
    } else {
      return;
    }
  }

  while (input_len > BLAKE3_CHUNK_LEN) {
    size_t   subtree_len  = round_down_to_power_of_2(input_len);
    uint64_t count_so_far = self->chunk.chunk_counter * BLAKE3_CHUNK_LEN;
    uint64_t subtree_chunks;

    while ((((uint64_t)(subtree_len - 1)) & count_so_far) != 0) {
      subtree_len /= 2;
    }
    subtree_chunks = subtree_len / BLAKE3_CHUNK_LEN;
    if (subtree_len <= BLAKE3_CHUNK_LEN) {
      struct Blake3ChunkState chunk_state;
      struct Output           output;
      uint8_t                 cv[BLAKE3_OUT_LEN];

      chunk_state_init(&chunk_state, self->key, self->chunk.flags);
      chunk_state.chunk_counter = self->chunk.chunk_counter;
      chunk_state_update(&chunk_state, input_bytes, subtree_len);
      output = chunk_state_output(&chunk_state);
      output_chaining_value(&output, cv);
      hasher_push_cv(self, cv, chunk_state.chunk_counter);
    } else {
      uint8_t cv_pair[2 * BLAKE3_OUT_LEN];

      compress_subtree_to_parent_node(
          input_bytes, subtree_len, self->key, self->chunk.chunk_counter, self->chunk.flags, cv_pair
      );
      hasher_push_cv(self, cv_pair, self->chunk.chunk_counter);
      hasher_push_cv(
          self, &cv_pair[BLAKE3_OUT_LEN], self->chunk.chunk_counter + (subtree_chunks / 2)
      );
    }
    self->chunk.chunk_counter += subtree_chunks;
    input_bytes += subtree_len;
    input_len -= subtree_len;
  }

  if (input_len > 0) {
    chunk_state_update(&self->chunk, input_bytes, input_len);
    hasher_merge_cv_stack(self, self->chunk.chunk_counter);
  }
}

void
blake3_hasher_finalize_seek(
    const struct Blake3Hasher *self, uint64_t seek, uint8_t *out, size_t out_len
)
{
  struct Output output;
  size_t        cvs_remaining;

  if (out_len == 0) {
    return;
  }

  if (self->cv_stack_len == 0) {
    output = chunk_state_output(&self->chunk);
    output_root_bytes(&output, seek, out, out_len);
    return;
  }

  if (chunk_state_len(&self->chunk) > 0) {
    cvs_remaining = self->cv_stack_len;
    output        = chunk_state_output(&self->chunk);
  } else {
    cvs_remaining = self->cv_stack_len - 2;
    output = parent_output(&self->cv_stack[cvs_remaining * 32], self->key, self->chunk.flags);
  }

  while (cvs_remaining > 0) {
    uint8_t parent_block[BLAKE3_BLOCK_LEN];

    cvs_remaining -= 1;
    memcpy(parent_block, &self->cv_stack[cvs_remaining * 32], 32);
    output_chaining_value(&output, &parent_block[32]);
    output = parent_output(parent_block, self->key, self->chunk.flags);
  }
  output_root_bytes(&output, seek, out, out_len);
}

void
blake3_hasher_finalize(const struct Blake3Hasher *self, uint8_t *out, size_t out_len)
{
  blake3_hasher_finalize_seek(self, 0, out, out_len);
}

void
blake3_hasher_reset(struct Blake3Hasher *self)
{
  chunk_state_reset(&self->chunk, self->key, 0);
  self->cv_stack_len = 0;
}

