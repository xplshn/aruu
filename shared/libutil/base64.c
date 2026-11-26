/* see LICENSE file for copyright and license details */
#include <stddef.h>
#include <stdint.h>

#include "../base64.h"

static const char alphabet[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t
base64_encode(char *out, const uint8_t *in, size_t n)
{
  size_t i, o = 0;

  for (i = 0; i + 2 < n; i += 3) {
    out[o++] = alphabet[in[i] >> 2];
    out[o++] = alphabet[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
    out[o++] = alphabet[((in[i + 1] & 15) << 2) | (in[i + 2] >> 6)];
    out[o++] = alphabet[in[i + 2] & 63];
  }
  if (i < n) {
    out[o++] = alphabet[in[i] >> 2];
    if (i + 1 < n) {
      out[o++] = alphabet[((in[i] & 3) << 4) | (in[i + 1] >> 4)];
      out[o++] = alphabet[(in[i + 1] & 15) << 2];
    } else {
      out[o++] = alphabet[(in[i] & 3) << 4];
      out[o++] = '=';
    }
    out[o++] = '=';
  }
  out[o] = '\0';
  return o;
}

static int
value(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

long
base64_decode(uint8_t *out, size_t cap, const char *in, size_t n)
{
  uint32_t acc = 0;
  size_t i, o = 0;
  int bits = 0, pad = 0, v;

  for (i = 0; i < n; i++) {
    if (in[i] == ' ' || in[i] == '\t' || in[i] == '\r' || in[i] == '\n')
      continue;
    if (in[i] == '=') {
      pad++;
      continue;
    }
    if (pad || (v = value(in[i])) < 0)
      return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= cap)
        return -1;
      out[o++] = (acc >> bits) & 0xff;
    }
  }
  return pad > 2 ? -1 : (long)o;
}
