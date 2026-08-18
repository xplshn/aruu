/* see LICENSE file for copyright and license details */

/* deflate decoder adapted from puff.c, mark adlers rfc 1951 reference
 * decoder. zlib license: copyright (c) 2002-2013 mark adler, provided
 * as-is, free to use/alter/redistribute, origin not misrepresented,
 * this notice kept. see zlib.net */

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "util.h"

#define MAXBITS   15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES  (MAXLCODES + MAXDCODES)
#define FIXLCODES 288

struct InflateState {
  unsigned char *out;
  unsigned long  outlen;
  unsigned long  outcnt;

  const unsigned char *in;
  unsigned long         inlen;
  unsigned long         incnt;
  int                   bitbuf;
  int                   bitcnt;

  jmp_buf env;
};

struct Huffman {
  short *count;
  short *symbol;
};

static int
inflate_bits(struct InflateState *s, int need)
{
  long val;

  val = s->bitbuf;
  while (s->bitcnt < need) {
    if (s->incnt == s->inlen)
      longjmp(s->env, 1);
    val |= (long)(s->in[s->incnt++]) << s->bitcnt;
    s->bitcnt += 8;
  }

  s->bitbuf = (int)(val >> need);
  s->bitcnt -= need;

  return (int)(val & ((1L << need) - 1));
}

static int
inflate_stored(struct InflateState *s)
{
  unsigned len;

  s->bitbuf = 0;
  s->bitcnt = 0;

  if (s->incnt + 4 > s->inlen)
    return 2;
  len = s->in[s->incnt++];
  len |= s->in[s->incnt++] << 8;
  if (s->in[s->incnt++] != (~len & 0xff) || s->in[s->incnt++] != ((~len >> 8) & 0xff))
    return -2;

  if (s->incnt + len > s->inlen)
    return 2;
  if (s->out != NULL) {
    if (s->outcnt + len > s->outlen)
      return 1;
    while (len--)
      s->out[s->outcnt++] = s->in[s->incnt++];
  } else {
    s->outcnt += len;
    s->incnt += len;
  }

  return 0;
}

/* codes are bit-reversed on the wire, rebuilt one bit at a time here */
static int
inflate_decode(struct InflateState *s, const struct Huffman *h)
{
  int    len, code, first, count, index;
  int    bitbuf, left;
  short *next;

  bitbuf = s->bitbuf;
  left   = s->bitcnt;
  code = first = index = 0;
  len          = 1;
  next         = h->count + 1;
  for (;;) {
    while (left--) {
      code |= bitbuf & 1;
      bitbuf >>= 1;
      count = *next++;
      if (code - count < first) {
        s->bitbuf = bitbuf;
        s->bitcnt = (s->bitcnt - len) & 7;
        return h->symbol[index + (code - first)];
      }
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
      len++;
    }
    left = (MAXBITS + 1) - len;
    if (left == 0)
      break;
    if (s->incnt == s->inlen)
      longjmp(s->env, 1);
    bitbuf = s->in[s->incnt++];
    if (left > 8)
      left = 8;
  }
  return -10;
}

/* 0: complete code, > 0: incomplete but usable, < 0: over-subscribed */
static int
huffman_construct(struct Huffman *h, const short *length, int n)
{
  int   symbol, len, left;
  short offs[MAXBITS + 1];

  for (len = 0; len <= MAXBITS; len++)
    h->count[len] = 0;
  for (symbol = 0; symbol < n; symbol++)
    (h->count[length[symbol]])++;
  if (h->count[0] == n)
    return 0;

  left = 1;
  for (len = 1; len <= MAXBITS; len++) {
    left <<= 1;
    left -= h->count[len];
    if (left < 0)
      return left;
  }

  offs[1] = 0;
  for (len = 1; len < MAXBITS; len++)
    offs[len + 1] = offs[len] + h->count[len];

  for (symbol = 0; symbol < n; symbol++)
    if (length[symbol] != 0)
      h->symbol[offs[length[symbol]]++] = symbol;

  return left;
}

static int
inflate_codes(struct InflateState *s, const struct Huffman *lencode, const struct Huffman *distcode)
{
  int            symbol, len;
  unsigned       dist;
  static const short lens[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,
                                  23, 27, 31, 35, 43, 51, 59, 67, 83,  99,  115, 131,
                                  163, 195, 227, 258};
  static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  static const short dists[30] = {1,    2,    3,    4,    5,    7,    9,    13,   17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,  513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3,  3,  4,  4,  5,  5,  6,
                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  do {
    symbol = inflate_decode(s, lencode);
    if (symbol < 0)
      return symbol;
    if (symbol < 256) {
      if (s->out != NULL) {
        if (s->outcnt == s->outlen)
          return 1;
        s->out[s->outcnt] = symbol;
      }
      s->outcnt++;
    } else if (symbol > 256) {
      symbol -= 257;
      if (symbol >= 29)
        return -10;
      len = lens[symbol] + inflate_bits(s, lext[symbol]);

      symbol = inflate_decode(s, distcode);
      if (symbol < 0)
        return symbol;
      dist = dists[symbol] + inflate_bits(s, dext[symbol]);
      if (dist > s->outcnt)
        return -11;

      if (s->out != NULL) {
        if (s->outcnt + len > s->outlen)
          return 1;
        while (len--) {
          s->out[s->outcnt] = s->out[s->outcnt - dist];
          s->outcnt++;
        }
      } else {
        s->outcnt += len;
      }
    }
  } while (symbol != 256);

  return 0;
}

static int
inflate_fixed(struct InflateState *s)
{
  static int          built;
  static short        lencnt[MAXBITS + 1], lensym[FIXLCODES];
  static short        distcnt[MAXBITS + 1], distsym[MAXDCODES];
  static struct Huffman lencode, distcode;

  if (!built) {
    int   symbol;
    short lengths[FIXLCODES];

    lencode.count  = lencnt;
    lencode.symbol = lensym;
    distcode.count  = distcnt;
    distcode.symbol = distsym;

    for (symbol = 0; symbol < 144; symbol++)
      lengths[symbol] = 8;
    for (; symbol < 256; symbol++)
      lengths[symbol] = 9;
    for (; symbol < 280; symbol++)
      lengths[symbol] = 7;
    for (; symbol < FIXLCODES; symbol++)
      lengths[symbol] = 8;
    huffman_construct(&lencode, lengths, FIXLCODES);

    for (symbol = 0; symbol < MAXDCODES; symbol++)
      lengths[symbol] = 5;
    huffman_construct(&distcode, lengths, MAXDCODES);

    built = 1;
  }

  return inflate_codes(s, &lencode, &distcode);
}

static int
inflate_dynamic(struct InflateState *s)
{
  int            nlen, ndist, ncode, index, err;
  short          lengths[MAXCODES];
  short          lencnt[MAXBITS + 1], lensym[MAXLCODES];
  short          distcnt[MAXBITS + 1], distsym[MAXDCODES];
  struct Huffman lencode, distcode;
  static const short order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                   11, 4,  12, 3, 13, 2, 14, 1, 15};

  lencode.count  = lencnt;
  lencode.symbol = lensym;
  distcode.count  = distcnt;
  distcode.symbol = distsym;

  nlen  = inflate_bits(s, 5) + 257;
  ndist = inflate_bits(s, 5) + 1;
  ncode = inflate_bits(s, 4) + 4;
  if (nlen > MAXLCODES || ndist > MAXDCODES)
    return -3;

  for (index = 0; index < ncode; index++)
    lengths[order[index]] = inflate_bits(s, 3);
  for (; index < 19; index++)
    lengths[order[index]] = 0;

  err = huffman_construct(&lencode, lengths, 19);
  if (err != 0)
    return -4;

  index = 0;
  while (index < nlen + ndist) {
    int symbol, len;

    symbol = inflate_decode(s, &lencode);
    if (symbol < 0)
      return symbol;
    if (symbol < 16) {
      lengths[index++] = symbol;
    } else {
      len = 0;
      if (symbol == 16) {
        if (index == 0)
          return -5;
        len    = lengths[index - 1];
        symbol = 3 + inflate_bits(s, 2);
      } else if (symbol == 17) {
        symbol = 3 + inflate_bits(s, 3);
      } else {
        symbol = 11 + inflate_bits(s, 7);
      }
      if (index + symbol > nlen + ndist)
        return -6;
      while (symbol--)
        lengths[index++] = len;
    }
  }

  if (lengths[256] == 0)
    return -9;

  err = huffman_construct(&lencode, lengths, nlen);
  if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
    return -7;

  err = huffman_construct(&distcode, lengths + nlen, ndist);
  if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
    return -8;

  return inflate_codes(s, &lencode, &distcode);
}

/* raw deflate (rfc 1951) decoder; dest null just computes destlen */
static int
inflate_raw(unsigned char *dest, unsigned long *destlen, const unsigned char *source,
            unsigned long *sourcelen)
{
  struct InflateState s;
  int                 last, type, err;

  s.out    = dest;
  s.outlen = *destlen;
  s.outcnt = 0;

  s.in     = source;
  s.inlen  = *sourcelen;
  s.incnt  = 0;
  s.bitbuf = 0;
  s.bitcnt = 0;

  if (setjmp(s.env) != 0) {
    err = 2;
  } else {
    do {
      last = inflate_bits(&s, 1);
      type = inflate_bits(&s, 2);
      err  = type == 0   ? inflate_stored(&s)
             : type == 1 ? inflate_fixed(&s)
             : type == 2 ? inflate_dynamic(&s)
                          : -1;
      if (err != 0)
        break;
    } while (!last);
  }

  if (err <= 0) {
    *destlen   = s.outcnt;
    *sourcelen = s.incnt;
  }
  return err;
}

static uint32_t crc32_table[256];

static void
crc32_init(void)
{
  uint32_t c;
  int      n, k;

  for (n = 0; n < 256; n++) {
    c = (uint32_t)n;
    for (k = 0; k < 8; k++)
      c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
    crc32_table[n] = c;
  }
}

static uint32_t
crc32_update(uint32_t crc, const unsigned char *buf, size_t len)
{
  crc = ~crc;
  while (len--)
    crc = crc32_table[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

static uint32_t
get_le32(const unsigned char *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
put_le32(unsigned char *p, uint32_t v)
{
  p[0] = v & 0xff;
  p[1] = (v >> 8) & 0xff;
  p[2] = (v >> 16) & 0xff;
  p[3] = (v >> 24) & 0xff;
}

/* FLG bits in the gzip header (rfc 1952) */
enum GzipFlag {
  GZ_FTEXT    = 1 << 0,
  GZ_FHCRC    = 1 << 1,
  GZ_FEXTRA   = 1 << 2,
  GZ_FNAME    = 1 << 3,
  GZ_FCOMMENT = 1 << 4,
};

static unsigned char *
read_all(FILE *fp, size_t *len)
{
  unsigned char *buf;
  size_t         cap, n;
  ssize_t        r;

  cap = 1 << 16;
  buf = emalloc(cap);
  n   = 0;
  for (;;) {
    if (n == cap) {
      cap *= 2;
      buf = erealloc(buf, cap);
    }
    r = fread(buf + n, 1, cap - n, fp);
    if (r <= 0)
      break;
    n += (size_t)r;
  }
  if (ferror(fp))
    eprintf("read:");
  *len = n;
  return buf;
}

/* decodes one gzip member; multi-member streams are not supported */
static void
gunzip(const unsigned char *in, size_t inlen, FILE *out)
{
  size_t   pos, hdr;
  unsigned flg;
  unsigned char *dest;
  unsigned long  destlen, srclen;
  uint32_t       want_crc, want_isize, got_crc;
  int            err;

  if (inlen < 18 || in[0] != 0x1f || in[1] != 0x8b)
    eprintf("gzip: not in gzip format\n");
  if (in[2] != 8)
    eprintf("gzip: unsupported compression method\n");

  flg = in[3];
  pos = 10;

  if (flg & GZ_FEXTRA) {
    unsigned xlen;
    if (pos + 2 > inlen)
      eprintf("gzip: truncated header\n");
    xlen = in[pos] | (in[pos + 1] << 8);
    pos += 2 + xlen;
  }
  if (flg & GZ_FNAME) {
    while (pos < inlen && in[pos] != '\0')
      pos++;
    pos++;
  }
  if (flg & GZ_FCOMMENT) {
    while (pos < inlen && in[pos] != '\0')
      pos++;
    pos++;
  }
  if (flg & GZ_FHCRC)
    pos += 2;
  if (pos + 8 > inlen)
    eprintf("gzip: truncated header\n");

  hdr = pos;

  /* first pass just measures destlen, second pass does the real decode */
  destlen = 0;
  srclen  = (unsigned long)(inlen - hdr - 8);
  err     = inflate_raw(NULL, &destlen, in + hdr, &srclen);
  if (err != 0)
    eprintf("gzip: corrupt compressed data\n");

  dest    = emalloc(destlen ? destlen : 1);
  srclen  = (unsigned long)(inlen - hdr - 8);
  destlen = destlen ? destlen : 1;
  err     = inflate_raw(dest, &destlen, in + hdr, &srclen);
  if (err != 0)
    eprintf("gzip: corrupt compressed data\n");

  want_crc   = get_le32(in + hdr + srclen);
  want_isize = get_le32(in + hdr + srclen + 4);
  got_crc    = crc32_update(0, dest, destlen);
  if (got_crc != want_crc)
    eprintf("gzip: crc mismatch\n");
  if ((uint32_t)destlen != want_isize)
    eprintf("gzip: size mismatch\n");

  if (fwrite(dest, 1, destlen, out) != destlen)
    eprintf("write:");
  free(dest);
}

/* stored (uncompressed) deflate blocks: no compression, always valid */
static void
gzip_write_stored(const unsigned char *in, size_t len, FILE *out)
{
  unsigned char hdr[5];
  size_t        chunk;
  int           last;

  if (len == 0) {
    hdr[0] = 1;
    hdr[1] = hdr[2] = hdr[3] = hdr[4] = 0;
    if (fwrite(hdr, 1, 5, out) != 5)
      eprintf("write:");
    return;
  }

  while (len > 0) {
    chunk = len > 65535 ? 65535 : len;
    last  = chunk == len;

    hdr[0] = last ? 1 : 0;
    hdr[1] = chunk & 0xff;
    hdr[2] = (chunk >> 8) & 0xff;
    hdr[3] = (~chunk) & 0xff;
    hdr[4] = ((~chunk) >> 8) & 0xff;

    if (fwrite(hdr, 1, 5, out) != 5 || fwrite(in, 1, chunk, out) != chunk)
      eprintf("write:");

    in += chunk;
    len -= chunk;
  }
}

static void
gzip_compress(const unsigned char *in, size_t len, const char *name, FILE *out)
{
  unsigned char hdr[10];
  unsigned char trl[8];
  uint32_t      crc;

  hdr[0] = 0x1f;
  hdr[1] = 0x8b;
  hdr[2] = 8;
  hdr[3] = name ? GZ_FNAME : 0;
  put_le32(hdr + 4, 0);
  hdr[8] = 0;
  hdr[9] = 255;
  if (fwrite(hdr, 1, 10, out) != 10)
    eprintf("write:");
  if (name && (fwrite(name, 1, strlen(name) + 1, out) != strlen(name) + 1))
    eprintf("write:");

  gzip_write_stored(in, len, out);

  crc = crc32_update(0, in, len);
  put_le32(trl, crc);
  put_le32(trl + 4, (uint32_t)len);
  if (fwrite(trl, 1, 8, out) != 8)
    eprintf("write:");
}

static char *
strip_gz_suffix(const char *name)
{
  size_t n = strlen(name);
  if (n > 3 && strcmp(name + n - 3, ".gz") == 0)
    return estrndup(name, n - 3);
  return NULL;
}

static void
usage(void)
{
  eprintf("usage: %s [-cdfkn] [-1..-9] [file ...]\n", argv0);
}

// ?man gzip: compress or decompress files
// ?man arguments: [file ...]
// ?man with no files, reads standard input and writes standard output
// ?man compressing replaces each file with file.gz; decompressing
// ?man reverses that. only single-member gzip streams are read back
int
main(int argc, char *argv[])
{
  int  dflag = 0, cflag = 0, fflag = 0, kflag = 0, nflag = 0;
  int  i;
  char outname[4096];

  ARGBEGIN
  {
    // ?man -c: write to standard output, keep the input file
    case 'c':
      cflag = 1;
      break;
    // ?man -d: decompress instead of compress
    case 'd':
      dflag = 1;
      break;
    // ?man -f: accepted for compatibility, no effect
    case 'f':
      fflag = 1;
      break;
    // ?man -k: keep the input file instead of removing it
    case 'k':
      kflag = 1;
      break;
    // ?man -n: omit the original file name from the header
    case 'n':
      nflag = 1;
      break;
    // ?man -1: accepted for compatibility, no effect (stored blocks only)
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      break;
    default:
      usage();
  }
  ARGEND

  (void)fflag;
  crc32_init();

  if (argc == 0) {
    unsigned char *buf;
    size_t         len;

    buf = read_all(stdin, &len);
    if (dflag)
      gunzip(buf, len, stdout);
    else
      gzip_compress(buf, len, NULL, stdout);
    free(buf);
    return 0;
  }

  for (i = 0; i < argc; i++) {
    FILE          *fp;
    unsigned char *buf;
    size_t         len;
    char          *stripped;

    fp = fopen(argv[i], "rb");
    if (!fp) {
      weprintf("open %s:", argv[i]);
      continue;
    }
    buf = read_all(fp, &len);
    fclose(fp);

    if (dflag) {
      if (cflag) {
        gunzip(buf, len, stdout);
      } else {
        stripped = strip_gz_suffix(argv[i]);
        if (!stripped)
          eprintf("gzip: %s: unknown suffix, ignored\n", argv[i]);
        strlcpy(outname, stripped, sizeof(outname));
        free(stripped);

        fp = fopen(outname, "wb");
        if (!fp)
          eprintf("open %s:", outname);
        gunzip(buf, len, fp);
        if (fclose(fp))
          eprintf("close %s:", outname);
        if (!kflag && unlink(argv[i]) < 0)
          weprintf("unlink %s:", argv[i]);
      }
    } else {
      if (cflag) {
        gzip_compress(buf, len, NULL, stdout);
      } else {
        strlcpy(outname, argv[i], sizeof(outname));
        strlcat(outname, ".gz", sizeof(outname));

        fp = fopen(outname, "wb");
        if (!fp)
          eprintf("open %s:", outname);
        gzip_compress(buf, len, nflag ? NULL : argv[i], fp);
        if (fclose(fp))
          eprintf("close %s:", outname);
        if (!kflag && unlink(argv[i]) < 0)
          weprintf("unlink %s:", argv[i]);
      }
    }

    free(buf);
  }

  return 0;
}
