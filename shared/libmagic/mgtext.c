/* see LICENSE file for copyright and license details */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mgint.h"

#include <stdlib.h>
#include <string.h>

/* a line longer than this is reported as very long */
#define MAXLINELEN 300

#define JSON_MAXDEPTH 500
#define CSV_LINES     10

/* what a byte is in text: never, ascii, iso-8859 or other extended ascii */
enum TextClass { TC_F, TC_T, TC_I, TC_X };

static const unsigned char text_chars[256] = {
    /* bel bs ht lf vt ff cr are text, so are sub and esc */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    0,
    /* nel, 0x85, counts as ascii text */
    3,
    3,
    3,
    3,
    3,
    1,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
};

/* the dd(1) ebcdic to ascii table of posix p1003.2 draft 11.2 */
static const unsigned char ebcdic_to_ascii[256] = {
    0,   1,   2,   3,   156, 9,   134, 127, 151, 141, 142, 11,   12,  13,  14,  15,   16,  17,  18,
    19,  157, 133, 8,   135, 24,  25,  146, 143, 28,  29,  30,   31,  128, 129, 130,  131, 132, 10,
    23,  27,  136, 137, 138, 139, 140, 5,   6,   7,   144, 145,  22,  147, 148, 149,  150, 4,   152,
    153, 154, 155, 20,  21,  158, 26,  ' ', 160, 161, 162, 163,  164, 165, 166, 167,  168, 213, '.',
    '<', '(', '+', '|', '&', 169, 170, 171, 172, 173, 174, 175,  176, 177, '!', '$',  '*', ')', ';',
    '~', '-', '/', 178, 179, 180, 181, 182, 183, 184, 185, 203,  ',', '%', '_', '>',  '?', 186, 187,
    188, 189, 190, 191, 192, 193, 194, '`', ':', '#', '@', '\'', '=', '"', 195, 'a',  'b', 'c', 'd',
    'e', 'f', 'g', 'h', 'i', 196, 197, 198, 199, 200, 201, 202,  'j', 'k', 'l', 'm',  'n', 'o', 'p',
    'q', 'r', '^', 204, 205, 206, 207, 208, 209, 229, 's', 't',  'u', 'v', 'w', 'x',  'y', 'z', 210,
    211, 212, '[', 214, 215, 216, 217, 218, 219, 220, 221, 222,  223, 224, 225, 226,  227, 228, ']',
    230, 231, '{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',  232, 233, 234, 235,  236, 237, '}',
    'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 238, 239, 240,  241, 242, 243, '\\', 159, 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z', 244, 245, 246, 247, 248, 249,  '0', '1', '2', '3',  '4', '5', '6',
    '7', '8', '9', 250, 251, 252, 253, 254, 255,
};

/* every byte is of class at most max */
static int
looks_class(const unsigned char *buf, size_t n, uint32_t *ubuf, size_t *ulen, int max)
{
  size_t i;
  int    t;

  *ulen = 0;
  for (i = 0; i < n; i++) {
    t = text_chars[buf[i]];
    if (t == TC_F || t > max)
      return 0;
    ubuf[(*ulen)++] = buf[i];
  }
  return 1;
}

/*
 * -1 invalid utf-8, 0 valid but with odd control characters, 1 plain
 * ascii, 2 utf-8 with multibyte sequences. decodes into ubuf if given
 */
int
mg_looks_utf8(const unsigned char *buf, size_t n, uint32_t *ubuf, size_t *ulen)
{
  uint32_t c;
  size_t   i;
  int      following, k, gotone = 0, ctrl = 0;
  unsigned lo, hi;

  if (ubuf)
    *ulen = 0;
  for (i = 0; i < n; i++) {
    if (!(buf[i] & 0x80)) {
      if (text_chars[buf[i]] != TC_T)
        ctrl = 1;
      if (ubuf)
        ubuf[(*ulen)++] = buf[i];
      continue;
    }
    if (!(buf[i] & 0x40))
      return -1;
    /* the second byte of these leads is narrower, so overlongs and surrogates fail */
    lo = 0x80;
    hi = 0xbf;
    if (buf[i] < 0xc2 || buf[i] > 0xf4)
      return -1;
    if (buf[i] == 0xe0)
      lo = 0xa0;
    else if (buf[i] == 0xed)
      hi = 0x9f;
    else if (buf[i] == 0xf0)
      lo = 0x90;
    else if (buf[i] == 0xf4)
      hi = 0x8f;
    if (!(buf[i] & 0x20)) {
      c         = buf[i] & 0x1f;
      following = 1;
    } else if (!(buf[i] & 0x10)) {
      c         = buf[i] & 0x0f;
      following = 2;
    } else {
      c         = buf[i] & 0x07;
      following = 3;
    }
    for (k = 0; k < following; k++) {
      if (++i >= n)
        goto done;
      if (k == 0 && (buf[i] < lo || buf[i] > hi))
        return -1;
      if (!(buf[i] & 0x80) || (buf[i] & 0x40))
        return -1;
      c = (c << 6) + (buf[i] & 0x3f);
    }
    if (ubuf)
      ubuf[(*ulen)++] = c;
    gotone = 1;
  }
done:
  return ctrl ? 0 : (gotone ? 2 : 1);
}

static int
looks_utf7(const unsigned char *buf, size_t n, size_t *ulen)
{
  if (n > 4 && buf[0] == '+' && buf[1] == '/' && buf[2] == 'v') {
    switch (buf[3]) {
      case '8':
      case '9':
      case '+':
      case '/':
        *ulen = 0;
        return 1;
      default:
        break;
    }
  }
  return -1;
}

static int
looks_ucs16(const unsigned char *b, size_t n, uint32_t *ubuf, size_t *ulen)
{
  uint32_t uc, hi = 0;
  size_t   i;
  int      be;

  if (n < 2)
    return 0;
  if (b[0] == 0xff && b[1] == 0xfe)
    be = 0;
  else if (b[0] == 0xfe && b[1] == 0xff)
    be = 1;
  else
    return 0;
  *ulen = 0;
  for (i = 2; i + 1 < n; i += 2) {
    uc = be ? (uint32_t)(b[i] << 8 | b[i + 1]) : (uint32_t)(b[i + 1] << 8 | b[i]);
    if (uc == 0xfffe || uc == 0xffff || (uc >= 0xfdd0 && uc <= 0xfdef))
      return 0;
    if (hi) {
      if (uc < 0xdc00 || uc > 0xdfff)
        return 0;
      uc = 0x10000 + 0x400 * (hi - 1) + (uc - 0xdc00);
      hi = 0;
    }
    if (uc < 128 && text_chars[uc] != TC_T)
      return 0;
    ubuf[(*ulen)++] = uc;
    if (uc >= 0xd800 && uc <= 0xdbff)
      hi = uc - 0xd800 + 1;
    if (uc >= 0xdc00 && uc <= 0xdfff)
      return 0;
  }
  return 1 + be;
}

static int
looks_ucs32(const unsigned char *b, size_t n, uint32_t *ubuf, size_t *ulen)
{
  uint32_t uc;
  size_t   i;
  int      be;

  if (n < 4)
    return 0;
  if (b[0] == 0xff && b[1] == 0xfe && b[2] == 0 && b[3] == 0)
    be = 0;
  else if (b[0] == 0 && b[1] == 0 && b[2] == 0xfe && b[3] == 0xff)
    be = 1;
  else
    return 0;
  *ulen = 0;
  for (i = 4; i + 3 < n; i += 4) {
    if (be)
      uc = (uint32_t)b[i] << 24 | (uint32_t)b[i + 1] << 16 | (uint32_t)b[i + 2] << 8 | b[i + 3];
    else
      uc = (uint32_t)b[i + 3] << 24 | (uint32_t)b[i + 2] << 16 | (uint32_t)b[i + 1] << 8 | b[i];
    ubuf[(*ulen)++] = uc;
    if (uc == 0xfffe || (uc < 128 && text_chars[uc] != TC_T))
      return 0;
  }
  return 1 + be;
}

/*
 * find the character set of buf, as file(1) names it in code and in
 * code_mime, and decode it into *ubufp (freed by the caller) unless
 * ubufp is NULL. returns 1 if the bytes look like text
 */
int
mg_encoding(
    const unsigned char *buf,
    size_t               n,
    uint32_t           **ubufp,
    size_t              *ulenp,
    const char         **code,
    const char         **code_mime,
    const char         **type
)
{
  unsigned char *nbuf;
  uint32_t      *ubuf;
  size_t         ulen = 0, i;
  int            rv   = 1, t;

  *type      = "text";
  *code      = "unknown";
  *code_mime = "binary";
  if (ubufp)
    *ubufp = NULL;
  if (ulenp)
    *ulenp = 0;
  if (n > MG_ENCODING_MAX)
    n = MG_ENCODING_MAX;
  if (!(ubuf = calloc(n + 1, sizeof(*ubuf))))
    return -1;
  if (looks_class(buf, n, ubuf, &ulen, TC_T)) {
    if (looks_utf7(buf, n, &ulen) > 0) {
      *code      = "Unicode text, UTF-7";
      *code_mime = "utf-7";
    } else {
      *code      = "ASCII";
      *code_mime = "us-ascii";
    }
  } else if (n > 3 && buf[0] == 0xef && buf[1] == 0xbb && buf[2] == 0xbf
             && mg_looks_utf8(buf + 3, n - 3, ubuf, &ulen) > 0) {
    *code      = "Unicode text, UTF-8 (with BOM)";
    *code_mime = "utf-8";
  } else if (mg_looks_utf8(buf, n, ubuf, &ulen) > 1) {
    *code      = "Unicode text, UTF-8";
    *code_mime = "utf-8";
  } else if ((t = looks_ucs32(buf, n, ubuf, &ulen)) != 0) {
    *code = t == 1 ? "Unicode text, UTF-32, little-endian" : "Unicode text, UTF-32, big-endian";
    *code_mime = t == 1 ? "utf-32le" : "utf-32be";
  } else if ((t = looks_ucs16(buf, n, ubuf, &ulen)) != 0) {
    *code = t == 1 ? "Unicode text, UTF-16, little-endian" : "Unicode text, UTF-16, big-endian";
    *code_mime = t == 1 ? "utf-16le" : "utf-16be";
  } else if (looks_class(buf, n, ubuf, &ulen, TC_I)) {
    *code      = "ISO-8859";
    *code_mime = "iso-8859-1";
  } else if (looks_class(buf, n, ubuf, &ulen, TC_X)) {
    *code      = "Non-ISO extended-ASCII";
    *code_mime = "unknown-8bit";
  } else {
    if (!(nbuf = malloc(n + 1))) {
      free(ubuf);
      return -1;
    }
    for (i = 0; i < n; i++)
      nbuf[i] = ebcdic_to_ascii[buf[i]];
    if (looks_class(nbuf, n, ubuf, &ulen, TC_T)) {
      *code      = "EBCDIC";
      *code_mime = "ebcdic";
    } else if (looks_class(nbuf, n, ubuf, &ulen, TC_I)) {
      *code      = "International EBCDIC";
      *code_mime = "ebcdic";
    } else {
      rv    = 0;
      *type = "binary";
    }
    free(nbuf);
  }
  if (ubufp) {
    *ubufp = ubuf;
    *ulenp = ulen;
  } else {
    free(ubuf);
  }
  return rv;
}

static unsigned char *
encode_utf8(unsigned char *b, size_t len, const uint32_t *u, size_t n)
{
  unsigned char *end = b + len;
  uint32_t       c;
  size_t         i;
  int            k, extra;

  for (i = 0; i < n; i++) {
    c = u[i];
    if (c <= 0x7f) {
      if (end - b < 1)
        return NULL;
      *b++ = (unsigned char)c;
      continue;
    }
    if (c <= 0x7ff)
      extra = 1;
    else if (c <= 0xffff)
      extra = 2;
    else if (c <= 0x1fffff)
      extra = 3;
    else if (c <= 0x3ffffff)
      extra = 4;
    else if (c <= 0x7fffffff)
      extra = 5;
    else
      return NULL;
    if (end - b < extra + 1)
      return NULL;
    *b++ = (unsigned char)((0xff00 >> (extra + 1)) + (c >> (6 * extra)));
    for (k = extra - 1; k >= 0; k--)
      *b++ = (unsigned char)(((c >> (6 * k)) & 0x3f) + 0x80);
  }
  return b;
}

static size_t
trim_nuls(const unsigned char *buf, size_t n)
{
  while (n > 1 && buf[n - 1] == '\0')
    n--;
  return n;
}

static void
describe_terminators(struct MagicOut *o, size_t crlf, size_t cr, size_t lf, size_t nel)
{
  /* only line ends other than lf, or none at all, are worth naming */
  if (crlf || cr || nel || !lf) {
    mg_puts(o, ", with");
    if (!crlf && !cr && !nel && !lf) {
      mg_puts(o, " no");
    } else {
      if (crlf) {
        mg_puts(o, " CRLF");
        if (cr || lf || nel)
          mg_puts(o, ",");
      }
      if (cr) {
        mg_puts(o, " CR");
        if (lf || nel)
          mg_puts(o, ",");
      }
      if (lf) {
        mg_puts(o, " LF");
        if (nel)
          mg_puts(o, ",");
      }
      if (nel)
        mg_puts(o, " NEL");
    }
    mg_puts(o, " line terminators");
  }
}

/* describe text: its text tests, then its character set and line ends. 0 if not text */
int
mg_ascmagic(struct MagicRun *r, int text)
{
  struct MagicBuf bb;
  const char     *code, *code_mime, *type;
  unsigned char  *u8 = NULL, *u8end;
  uint32_t       *ubuf;
  size_t          n, ulen, i, crlf = 0, lf = 0, cr = 0, nel = 0, longest = 0, last = (size_t)-1;
  int             rv, seen_cr = 0, esc = 0, bs = 0, executable = 0;

  n = trim_nuls(r->head.p, r->head.n);
  /* dont lose the last character of utf-16 le text */
  if ((n & 1) && !(r->head.n & 1))
    n++;
  if ((rv = mg_encoding(r->head.p, n, &ubuf, &ulen, &code, &code_mime, &type)) <= 0) {
    free(ubuf);
    return rv;
  }
  rv = 0;
  if (trim_nuls(r->head.p, n) <= 1)
    goto done;
  if (ulen > 0) {
    if (!(u8 = malloc(ulen * 6)) || !(u8end = encode_utf8(u8, ulen * 6, ubuf, ulen))) {
      rv = u8 ? 0 : -1;
      goto done;
    }
    bb.p    = u8;
    bb.n    = (size_t)(u8end - u8);
    bb.file = r->head.file;
    if (mg_softmagic(r, &bb, MF_TEXTTEST, text) < 0) {
      rv = -1;
      goto done;
    }
  }
  for (i = 0; i < ulen; i++) {
    if (ubuf[i] == '\n') {
      if (seen_cr)
        crlf++;
      else
        lf++;
      last = i;
    } else if (seen_cr) {
      cr++;
    }
    seen_cr = ubuf[i] == '\r';
    if (seen_cr)
      last = i;
    if (ubuf[i] == 0x85) {
      nel++;
      last = i;
    }
    if (i > last + MAXLINELEN && i - last > longest)
      longest = i - last;
    if (ubuf[i] == '\033')
      esc = 1;
    if (ubuf[i] == '\b')
      bs = 1;
  }
  if (seen_cr && !cr && !crlf)
    cr++;
  rv = 1;
  if (r->mime) {
    if (!r->out.n)
      mg_puts(&r->out, "text/plain");
    goto done;
  }
  if (r->out.n) {
    if (!mg_replace_end(&r->out, " text", ", ")) {
      if (mg_replace_end(&r->out, " text executable", ", "))
        executable = 1;
      else
        mg_puts(&r->out, ", ");
    }
  }
  mg_puts(&r->out, code);
  mg_puts(&r->out, " ");
  mg_puts(&r->out, type);
  if (executable)
    mg_puts(&r->out, " executable");
  if (longest)
    mg_printf(&r->out, ", with very long lines (%lu)", (unsigned long)longest);
  describe_terminators(&r->out, crlf, cr, lf, nel);
  if (esc)
    mg_puts(&r->out, ", with escape sequences");
  if (bs)
    mg_puts(&r->out, ", with overstriking");
done:
  free(u8);
  free(ubuf);
  return rv;
}

/* tar: 1 old unix, 2 posix ustar, 3 gnu, 0 when the header checksum is wrong */
static int
from_oct(const unsigned char *p, size_t n)
{
  int v = 0;

  while (n > 0 && (*p == ' ' || (*p >= '\t' && *p <= '\r'))) {
    p++;
    n--;
  }
  if (!n)
    return -1;
  while (n > 0 && *p >= '0' && *p <= '7') {
    v = (v << 3) | (*p++ - '0');
    n--;
  }
  if (n > 0 && *p && !(*p == ' ' || (*p >= '\t' && *p <= '\r')))
    return -1;
  return v;
}

int
mg_istar(const unsigned char *b, size_t n)
{
  static const char    gpkg[] = "/gpkg-1";
  const unsigned char *nul;
  size_t               i;
  int                  sum = 0, rec;

  if (n < 512)
    return 0;
  /* gentoo binary packages look like tar, the magic tests tell them apart */
  nul = memchr(b, 0, 100);
  if (nul && nul >= b + sizeof(gpkg) && memcmp(nul - sizeof(gpkg) + 1, gpkg, sizeof(gpkg)) == 0)
    return 0;
  rec = from_oct(b + 148, 8);
  for (i = 0; i < 512; i++)
    sum += b[i];
  for (i = 148; i < 156; i++)
    sum -= b[i];
  sum += ' ' * 8;
  if (sum != rec)
    return 0;
  if (memcmp(b + 257, "ustar  ", 8) == 0)
    return 3;
  if (memcmp(b + 257, "ustar", 6) == 0)
    return 2;
  return 1;
}

/* json, a validating parser that gives up at the first error */
enum JsonCount { J_ARRAY, J_CONSTANT, J_NUMBER, J_OBJECT, J_STRING, J_ARRAYN, J_MAX };

static int json_parse(const unsigned char **, const unsigned char *, size_t *, size_t);

static int
json_space(int c)
{
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static int
json_xdigit(int c)
{
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static const unsigned char *
json_skip(const unsigned char *uc, const unsigned char *ue)
{
  while (uc < ue && json_space(*uc))
    uc++;
  return uc;
}

static int
json_string(const unsigned char **ucp, const unsigned char *ue)
{
  const unsigned char *uc = *ucp;
  size_t               i;

  while (uc < ue) {
    switch (*uc++) {
      case '\0':
        goto out;
      case '\\':
        if (uc == ue)
          goto out;
        switch (*uc++) {
          case '\0':
            goto out;
          case '"':
          case '\\':
          case '/':
          case 'b':
          case 'f':
          case 'n':
          case 'r':
          case 't':
            continue;
          case 'u':
            if (ue - uc < 4) {
              uc = ue;
              goto out;
            }
            for (i = 0; i < 4; i++) {
              if (!json_xdigit(*uc++))
                goto out;
            }
            continue;
          default:
            goto out;
        }
      case '"':
        *ucp = uc;
        return 1;
      default:
        continue;
    }
  }
out:
  *ucp = uc;
  return 0;
}

static int
json_array(const unsigned char **ucp, const unsigned char *ue, size_t *st, size_t lvl)
{
  const unsigned char *uc = *ucp;

  while (uc < ue) {
    uc = json_skip(uc, ue);
    if (uc == ue)
      goto out;
    if (*uc == ']')
      goto done;
    if (!json_parse(&uc, ue, st, lvl + 1) || uc == ue)
      goto out;
    switch (*uc) {
      case ',':
        uc++;
        continue;
      case ']':
      done:
        st[J_ARRAYN]++;
        *ucp = uc + 1;
        return 1;
      default:
        goto out;
    }
  }
out:
  *ucp = uc;
  return 0;
}

static int
json_object(const unsigned char **ucp, const unsigned char *ue, size_t *st, size_t lvl)
{
  const unsigned char *uc = *ucp;

  while (uc < ue) {
    uc = json_skip(uc, ue);
    if (uc == ue)
      goto out;
    if (*uc == '}') {
      *ucp = uc + 1;
      return 1;
    }
    if (*uc++ != '"' || !json_string(&uc, ue))
      goto out;
    uc = json_skip(uc, ue);
    if (uc == ue || *uc++ != ':')
      goto out;
    if (!json_parse(&uc, ue, st, lvl + 1) || uc == ue)
      goto out;
    switch (*uc++) {
      case ',':
        continue;
      case '}':
        *ucp = uc;
        return 1;
      default:
        *ucp = uc - 1;
        return 0;
    }
  }
out:
  *ucp = uc;
  return 0;
}

static int
json_digit(int c)
{
  return c >= '0' && c <= '9';
}

static int
json_number(const unsigned char **ucp, const unsigned char *ue)
{
  const unsigned char *uc  = *ucp;
  int                  got = 0;

  if (uc == ue)
    return 0;
  if (*uc == '-')
    uc++;
  for (; uc < ue && json_digit(*uc); uc++)
    got = 1;
  if (uc == ue)
    goto out;
  if (*uc == '.')
    uc++;
  for (; uc < ue && json_digit(*uc); uc++)
    got = 1;
  if (uc == ue)
    goto out;
  if (got && (*uc == 'e' || *uc == 'E')) {
    uc++;
    got = 0;
    if (uc == ue)
      goto out;
    if (*uc == '+' || *uc == '-')
      uc++;
    for (; uc < ue && json_digit(*uc); uc++)
      got = 1;
  }
out:
  if (got)
    *ucp = uc;
  return got;
}

/* true, false and null: file(1) compares all but the first letter */
static int
json_const(const unsigned char **ucp, const unsigned char *ue, const char *str)
{
  const unsigned char *uc  = *ucp;
  size_t               len = strlen(str);

  *ucp += len - 1;
  if (*ucp > ue)
    *ucp = ue;
  for (str++, len--; uc < ue && len; len--) {
    if (*uc++ != (unsigned char)*str++)
      return 0;
  }
  return 1;
}

static int
json_parse(const unsigned char **ucp, const unsigned char *ue, size_t *st, size_t lvl)
{
  const unsigned char *uc, *ouc;
  int                  rv = 0, t = J_NUMBER;

  ouc = uc = json_skip(*ucp, ue);
  if (uc == ue)
    goto out;
  if (lvl > JSON_MAXDEPTH)
    return 0;
  switch (*uc++) {
    case '"':
      rv = json_string(&uc, ue);
      t  = J_STRING;
      break;
    case '[':
      rv = json_array(&uc, ue, st, lvl + 1);
      t  = J_ARRAY;
      break;
    case '{':
      rv = json_object(&uc, ue, st, lvl + 1);
      t  = J_OBJECT;
      break;
    case 't':
      rv = json_const(&uc, ue, "true");
      t  = J_CONSTANT;
      break;
    case 'f':
      rv = json_const(&uc, ue, "false");
      t  = J_CONSTANT;
      break;
    case 'n':
      rv = json_const(&uc, ue, "null");
      t  = J_CONSTANT;
      break;
    default:
      --uc;
      rv = json_number(&uc, ue);
      break;
  }
  if (rv)
    st[t]++;
  uc = json_skip(uc, ue);
out:
  *ucp = uc;
  if (lvl == 0) {
    if (!rv)
      return 0;
    if (uc == ue)
      return (st[J_ARRAYN] || st[J_OBJECT]) ? 1 : 0;
    if (*ouc == *uc && json_parse(&uc, ue, st, 1))
      return (st[J_ARRAYN] || st[J_OBJECT]) ? 2 : 0;
    return 0;
  }
  return rv;
}

/* 1 for json, 2 for newline delimited json */
int
mg_isjson(const unsigned char *b, size_t n)
{
  size_t st[J_MAX];

  memset(st, 0, sizeof(st));
  return json_parse(&b, b + n, st, 0);
}

static const unsigned char *
csv_quote(const unsigned char *uc, const unsigned char *ue)
{
  int quote = 0;

  while (uc < ue) {
    if (*uc++ != '"') {
      if (quote)
        return uc - 1;
      continue;
    }
    /* two quotes in a row are an escaped quote */
    quote = !quote;
  }
  return ue;
}

/* comma separated values: the same number of fields on the first lines */
int
mg_iscsv(const unsigned char *uc, size_t n)
{
  const unsigned char *ue = uc + n;
  size_t               nf = 0, tf = 0, nl = 0;

  while (uc < ue) {
    switch (*uc++) {
      case '"':
        uc = csv_quote(uc, ue);
        break;
      case ',':
        nf++;
        break;
      case '\n':
        if (++nl == CSV_LINES)
          return tf > 1 && tf == nf;
        if (tf == 0) {
          if (nf == 0)
            return 0;
          tf = nf;
        } else if (tf != nf) {
          return 0;
        }
        nf = 0;
        break;
      default:
        break;
    }
  }
  return tf >= 1 && nl >= 2;
}
