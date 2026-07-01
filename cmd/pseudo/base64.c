/* See LICENSE file for copyright and license details. */

#include "arg.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
  eprintf("usage: %s [-d] [-i] [-w cols] [file]\n", argv0);
}

static void
base64_encode(char *dst, const unsigned char *src, size_t len)
{
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqr"
                            "stuvwxyz0123456789+/";
  size_t            i;

  for (i = 0; i < len; i += 3, dst += 4) {
    unsigned long x = (src[i] & 0xfful) << 16;
    dst[3]          = i + 2 >= len ? '=' : b64[(x |= src[i + 2] & 0xfful) & 0x3f];
    dst[2]          = i + 1 >= len ? '=' : b64[(x |= (src[i + 1] & 0xfful) << 8) >> 6 & 0x3f];
    dst[1]          = b64[x >> 12 & 0x3f];
    dst[0]          = b64[x >> 18];
  }
  *dst = '\0';
}

static size_t
base64_decode(unsigned char *dst, const char *src)
{
  static const char b64[] = {
      ['A'] = 0,  ['B'] = 1,  ['C'] = 2,  ['D'] = 3,  ['E'] = 4,  ['F'] = 5,  ['G'] = 6,
      ['H'] = 7,  ['I'] = 8,  ['J'] = 9,  ['K'] = 10, ['L'] = 11, ['M'] = 12, ['N'] = 13,
      ['O'] = 14, ['P'] = 15, ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19, ['U'] = 20,
      ['V'] = 21, ['W'] = 22, ['X'] = 23, ['Y'] = 24, ['Z'] = 25, ['a'] = 26, ['b'] = 27,
      ['c'] = 28, ['d'] = 29, ['e'] = 30, ['f'] = 31, ['g'] = 32, ['h'] = 33, ['i'] = 34,
      ['j'] = 35, ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40, ['p'] = 41,
      ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45, ['u'] = 46, ['v'] = 47, ['w'] = 48,
      ['x'] = 49, ['y'] = 50, ['z'] = 51, ['0'] = 52, 53,         54,         55,
      56,         57,         58,         59,         60,         61,         ['+'] = 62,
      ['/'] = 63, ['='] = 0,
  };
  unsigned long x;
  size_t        i, c, len, pad;

  for (i = 0, x = 0, len = 0, pad = 0; src[i]; ++i) {
    c = (unsigned char)src[i];
    if (c == '=' && (!src[i + 1] || (src[i + 1] == '=' && !src[i + 2])))
      ++pad;
    else if (c >= sizeof(b64) || (!b64[c] && c != 'A'))
      return 0;
    x = x << 6 | b64[c];
    if (i % 4 == 3) {
      dst[len + 2] = x & 0xff, x >>= 8;
      dst[len + 1] = x & 0xff, x >>= 8;
      dst[len + 0] = x & 0xff;
      len += 3;
    }
  }
  if (i % 4 != 0)
    return 0;
  return len - pad;
}

static void
encode_stream(FILE *fp, size_t wrap)
{
  unsigned char buf[3072];
  char          out[4096 + 1];
  size_t        n, i, col;

  col = 0;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
    base64_encode(out, buf, n);
    for (i = 0; out[i]; i++) {
      putchar(out[i]);
      if (wrap > 0 && ++col == wrap) {
        putchar('\n');
        col = 0;
      }
    }
  }
  if (wrap > 0 && col > 0)
    putchar('\n');
}

static void
decode_stream(FILE *fp, int iflag)
{
  char          in[5];
  unsigned char out[4];
  int           c;
  size_t        count, n;

  count = 0;
  while ((c = fgetc(fp)) != EOF) {
    /* skip whitespace */
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ')
      continue;
    /* check validity */
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+'
        || c == '/' || c == '=') {
      in[count++] = c;
      if (count == 4) {
        in[4] = '\0';
        n     = base64_decode(out, in);
        if (n > 0)
          fwrite(out, 1, n, stdout);
        else
          eprintf("invalid input\n");
        if (strchr(in, '='))
          break;
        count = 0;
      }
    } else if (!iflag) {
      eprintf("invalid character\n");
    }
  }
  if (count > 0)
    eprintf("input is truncated\n");
}

// ?man base64: encode or decode base64
// ?man arguments: file
// ?man encode or decode data in base64 format
int
main(int argc, char *argv[])
{
  FILE  *fp;
  int    dflag, iflag, ret;
  size_t wrap;

  dflag = 0;
  iflag = 0;
  wrap  = 76;
  ret   = 0;
  fp    = stdin;

  ARGBEGIN
  {
    // ?man -d: specify directory
    case 'd':
      dflag = 1;
      break;
    // ?man -i: interactive mode or prompt for confirmation
    case 'i':
      iflag = 1;
      break;
    // ?man -w:num: wait for completion
    case 'w':
      wrap = estrtonum(
          EARGF(usage()), 0, MIN((unsigned long long)LLONG_MAX, (unsigned long long)SSIZE_MAX)
      );
      break;
    default:
      usage();
  }
  ARGEND

  if (argc > 1)
    usage();

  if (argc == 1 && strcmp(argv[0], "-") != 0) {
    fp = fopen(argv[0], "r");
    if (!fp)
      eprintf("fopen %s:", argv[0]);
  }

  if (dflag)
    decode_stream(fp, iflag);
  else
    encode_stream(fp, wrap);

  if (fp != stdin)
    fclose(fp);

  ret = fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");
  return ret;
}
