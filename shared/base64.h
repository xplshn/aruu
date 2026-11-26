/* see LICENSE file for copyright and license details */
#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>
#include <stdint.h>

/* the length base64_encode writes for n bytes, its terminator not counted */
#define BASE64_LEN(n) ((((n) + 2) / 3) * 4)

/* rfc 4648 base64 of in into out, terminated. returns the length */
size_t base64_encode(char *out, const uint8_t *in, size_t n);

/* the bytes of the n characters of in, into out (cap bytes at most).
   blanks are skipped. returns their number, -1 on a bad or long input */
long base64_decode(uint8_t *out, size_t cap, const char *in, size_t n);

#endif
