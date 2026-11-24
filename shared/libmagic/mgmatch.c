/* see LICENSE file for copyright and license details */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mgint.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* longest string or number file(1) prints for one value */
#define SBUFLEN 512

/* largest valid time_t file(1) formats, dec 31 9999 */
#define MAX_CTIME 0x3afff487cfLL

/* seconds from 1601, the windows epoch, to 1970 */
#define WIN_EPOCH 11644473600LL

/* how a value is handed to the printf conversion of a description */
enum ValKind {
  VK_INT,   /* promoted to int, as file(1) passes bytes, shorts and longs */
  VK_UINT,  /* the same, from an unsigned type */
  VK_QUAD,  /* long long */
  VK_UQUAD, /* unsigned long long */
  VK_FLOAT, /* double */
  VK_STR
};

/* printed, separator and first line flags shared down a match and its use calls */
struct Shared {
  int printed;
  int need_sep;
  int firstline;
};

static int match(
    struct MagicRun       *r,
    struct MagicLine      *v,
    size_t                 nv,
    const struct MagicBuf *b,
    size_t                 o,
    int                    mode,
    int                    text,
    int                    flip,
    struct Shared         *sh,
    int                   *retp,
    int                   *foundp
);

static uint16_t
be16(const unsigned char *p)
{
  return (uint16_t)(p[0] << 8 | p[1]);
}

static uint16_t
le16(const unsigned char *p)
{
  return (uint16_t)(p[1] << 8 | p[0]);
}

static uint32_t
be32(const unsigned char *p)
{
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t
le32(const unsigned char *p)
{
  return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* pdp-11 middle endian */
static uint32_t
me32(const unsigned char *p)
{
  return (uint32_t)p[1] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[3] << 8 | p[2];
}

static uint64_t
be64(const unsigned char *p)
{
  return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static uint64_t
le64(const unsigned char *p)
{
  return (uint64_t)le32(p + 4) << 32 | le32(p);
}

static int64_t
sext(int sgn, int bits, uint64_t v)
{
  switch (bits) {
    case 8:
      return sgn ? (int64_t)(int8_t)v : (int64_t)(uint8_t)v;
    case 16:
      return sgn ? (int64_t)(int16_t)v : (int64_t)(uint16_t)v;
    case 32:
      return sgn ? (int64_t)(int32_t)v : (int64_t)(uint32_t)v;
    default:
      return (int64_t)v;
  }
}

static int
offset_oob(size_t n, int64_t o, size_t i)
{
  if (o < 0 || (uint64_t)o > n)
    return 1;
  return i > n - (size_t)o;
}

size_t
mg_pstrlen_size(uint32_t flags)
{
  switch (flags & SF_PSTRING_LEN) {
    case SF_PSTRING_1:
      return 1;
    case SF_PSTRING_2_LE:
    case SF_PSTRING_2_BE:
      return 2;
    case SF_PSTRING_4_LE:
    case SF_PSTRING_4_BE:
      return 4;
    default:
      return 0;
  }
}

static size_t
pstr_len(const struct MagicLine *m, const unsigned char *s, int *ok)
{
  size_t len, sz;

  *ok = 1;
  switch (m->sflags & SF_PSTRING_LEN) {
    case SF_PSTRING_1:
      len = s[0];
      break;
    case SF_PSTRING_2_LE:
      len = le16(s);
      break;
    case SF_PSTRING_2_BE:
      len = be16(s);
      break;
    case SF_PSTRING_4_LE:
      len = le32(s);
      break;
    case SF_PSTRING_4_BE:
      len = be32(s);
      break;
    default:
      *ok = 0;
      return 0;
  }
  if (m->sflags & SF_PSTRING_SELF) {
    sz = mg_pstrlen_size(m->sflags);
    len -= sz;
  }
  return len;
}

static int
flip_type(int t, int flip)
{
  if (!flip)
    return t;
  switch (t) {
    case MT_BESHORT:
      return MT_LESHORT;
    case MT_BELONG:
      return MT_LELONG;
    case MT_BEDATE:
      return MT_LEDATE;
    case MT_BELDATE:
      return MT_LELDATE;
    case MT_BEQUAD:
      return MT_LEQUAD;
    case MT_BEQDATE:
      return MT_LEQDATE;
    case MT_BEQLDATE:
      return MT_LEQLDATE;
    case MT_BEQWDATE:
      return MT_LEQWDATE;
    case MT_LESHORT:
      return MT_BESHORT;
    case MT_LELONG:
      return MT_BELONG;
    case MT_LEDATE:
      return MT_BEDATE;
    case MT_LELDATE:
      return MT_BELDATE;
    case MT_LEQUAD:
      return MT_BEQUAD;
    case MT_LEQDATE:
      return MT_BEQDATE;
    case MT_LEQLDATE:
      return MT_BEQLDATE;
    case MT_LEQWDATE:
      return MT_BEQWDATE;
    case MT_BEFLOAT:
      return MT_LEFLOAT;
    case MT_LEFLOAT:
      return MT_BEFLOAT;
    case MT_BEDOUBLE:
      return MT_LEDOUBLE;
    case MT_LEDOUBLE:
      return MT_BEDOUBLE;
    case MT_BEMSDOSDATE:
      return MT_LEMSDOSDATE;
    case MT_LEMSDOSDATE:
      return MT_BEMSDOSDATE;
    case MT_BEMSDOSTIME:
      return MT_LEMSDOSTIME;
    case MT_LEMSDOSTIME:
      return MT_BEMSDOSTIME;
    default:
      return t;
  }
}

/* the last bytes of the file, as many as the head holds, for offsets from the end */
static int
tail_fill(struct MagicRun *r)
{
  unsigned char *p;
  size_t         n;

  if (r->tstate)
    return r->tstate < 0 ? -1 : 0;
  r->tstate = -1;
  if (r->fsize < 0)
    return -1;
  n = (unsigned long long)r->fsize < r->head.n ? (size_t)r->fsize : r->head.n;
  if ((unsigned long long)r->fsize <= r->head.n) {
    r->tail   = (unsigned char *)r->head.p + (r->head.n - n);
    r->tlen   = n;
    r->tstate = 2;
    return 0;
  } else {
    if (!(p = malloc(n ? n : 1)))
      return -1;
    if (mg_pread(r, p, n, r->fsize - (long long)n) != (long long)n) {
      free(p);
      return -1;
    }
    r->tail = p;
    r->tlen = n;
  }
  r->tstate = 1;
  return 0;
}

static int
setoffset(
    struct MagicRun        *r,
    const struct MagicLine *m,
    struct MagicBuf        *bb,
    const struct MagicBuf  *b,
    size_t                  o,
    unsigned                cont
)
{
  int32_t offset;

  if (m->flag & MF_OFFNEG) {
    offset = -m->offset;
    if (cont > 0 && (m->flag & (MF_OFFADD | MF_INDIROFFADD)))
      goto normal;
    if (b->file && tail_fill(r) < 0)
      return -1;
    if (o != 0)
      return -1;
    if (!b->file) {
      if ((size_t)(uint32_t)m->offset > b->n)
        return -1;
      r->offset = r->eoffset = (int32_t)(b->n - (uint32_t)m->offset);
    } else {
      if ((size_t)(uint32_t)m->offset > r->tlen)
        return -1;
      bb->p     = r->tail;
      bb->n     = r->tlen;
      bb->file  = 1;
      r->offset = r->eoffset = (int32_t)(r->tlen - (uint32_t)m->offset);
    }
    return 0;
  }
  offset = m->offset;
  if ((m->flag & MF_OFFPOS) || cont == 0) {
  normal:
    *bb        = *b;
    r->offset  = offset;
    r->eoffset = 0;
  } else {
    r->offset = r->eoffset + offset;
  }
  return 0;
}

static void
copy_regex(
    struct MagicRun        *r,
    const struct MagicLine *m,
    const unsigned char    *s,
    uint32_t                offset,
    size_t                  nbytes
)
{
  const unsigned char *buf, *end, *last, *b, *c;
  size_t               lines, linecnt, bytecnt;

  if (!s || nbytes < offset) {
    r->slen = 0;
    r->ss   = NULL;
    return;
  }
  if (m->sflags & SF_REGEX_LINES) {
    linecnt = m->range;
    bytecnt = linecnt * 80;
  } else {
    linecnt = 0;
    bytecnt = m->range;
  }
  if (bytecnt == 0 || bytecnt > nbytes - offset)
    bytecnt = nbytes - offset;
  if (bytecnt > MG_REGEX_MAX)
    bytecnt = MG_REGEX_MAX;
  buf = s + offset;
  end = last = s + offset + bytecnt;
  for (lines = linecnt, b = buf; lines && b < end; lines--, b++) {
    c = b;
    if (!(b = memchr(c, '\n', (size_t)(end - c))) && !(b = memchr(c, '\r', (size_t)(end - c))))
      break;
    if (b < end - 1 && b[0] == '\r' && b[1] == '\n')
      b++;
    if (b < end - 1 && b[0] == '\n')
      b++;
    last = b;
  }
  if (lines)
    last = end;
  r->ss    = buf;
  r->slen  = (size_t)(last - buf);
  r->soff  = offset;
  r->rmlen = 0;
}

static void
copy_string16(struct MagicRun *r, int type, const unsigned char *s, uint32_t offset, size_t nbytes)
{
  const unsigned char *src = s + offset, *esrc = s + nbytes;
  char                *dst = r->val.s, *edst = &r->val.s[MG_STRLEN - 1];

  if (type == MT_BESTRING16)
    src++;
  for (; src < esrc; src += 2, dst++) {
    if (dst >= edst)
      break;
    *dst = (char)*src;
    if (!*dst) {
      if (type == MT_BESTRING16 ? src[-1] != '\0' : (src + 1 < esrc && src[1] != '\0'))
        *dst = ' ';
    }
  }
  *edst = '\0';
  *dst  = '\0';
}

/* fetch the bytes a test looks at into r->val, or set up the search window */
static void
mcopy(
    struct MagicRun        *r,
    int                     type,
    int                     indir,
    const unsigned char    *s,
    uint32_t                offset,
    size_t                  nbytes,
    const struct MagicLine *m
)
{
  size_t size = sizeof(r->val), n;

  if (!indir) {
    switch (type) {
      case MT_DER:
      case MT_SEARCH:
        if (offset > nbytes)
          offset = (uint32_t)nbytes;
        r->ss   = s + offset;
        r->slen = nbytes - offset;
        r->soff = offset;
        return;
      case MT_REGEX:
        copy_regex(r, m, s, offset, nbytes);
        return;
      case MT_BESTRING16:
      case MT_LESTRING16:
        if (offset < nbytes) {
          copy_string16(r, type, s, offset, nbytes);
          return;
        }
        break;
      case MT_STRING:
      case MT_PSTRING:
        if (m->range && m->range < sizeof(r->val))
          size = m->range;
        break;
      default:
        break;
    }
  }
  if (type == MT_OFFSET) {
    memset(&r->val, 0, sizeof(r->val));
    r->val.q = offset;
    return;
  }
  if (offset >= nbytes) {
    memset(&r->val, 0, sizeof(r->val));
    return;
  }
  n = nbytes - offset < size ? nbytes - offset : size;
  memcpy(&r->val, s + offset, n);
  if (n < sizeof(r->val))
    memset((char *)&r->val + n, 0, sizeof(r->val) - n);
}

/* apply a numeric mask operator, 0 on division by zero */
static int
domask_u(const struct MagicLine *m, uint64_t *x, uint64_t mask)
{
  if (m->mask) {
    switch (m->mask_op & OP_MASK) {
      case OP_AND:
        *x &= mask;
        break;
      case OP_OR:
        *x |= mask;
        break;
      case OP_XOR:
        *x ^= mask;
        break;
      case OP_ADD:
        *x += mask;
        break;
      case OP_MINUS:
        *x -= mask;
        break;
      case OP_MULTIPLY:
        *x *= mask;
        break;
      case OP_DIVIDE:
        if (!mask)
          return 0;
        *x /= mask;
        break;
      case OP_MODULO:
        if (!mask)
          return 0;
        *x %= mask;
        break;
    }
  }
  if (m->mask_op & OP_INVERSE)
    *x = ~*x;
  return 1;
}

static int
domask_s(const struct MagicLine *m, int64_t *x, int64_t mask, int64_t min)
{
  if (m->mask) {
    switch (m->mask_op & OP_MASK) {
      case OP_AND:
        *x &= mask;
        break;
      case OP_OR:
        *x |= mask;
        break;
      case OP_XOR:
        *x ^= mask;
        break;
      case OP_ADD:
        *x = (int64_t)((uint64_t)*x + (uint64_t)mask);
        break;
      case OP_MINUS:
        *x = (int64_t)((uint64_t)*x - (uint64_t)mask);
        break;
      case OP_MULTIPLY:
        *x = (int64_t)((uint64_t)*x * (uint64_t)mask);
        break;
      case OP_DIVIDE:
        if (!mask || (*x == min && mask == -1))
          return 0;
        *x /= mask;
        break;
      case OP_MODULO:
        if (!mask || (*x == min && mask == -1))
          return 0;
        *x %= mask;
        break;
    }
  }
  if (m->mask_op & OP_INVERSE)
    *x = ~*x;
  return 1;
}

/* masks act at the width of the type, signed unless the type is unsigned */
static int
cvt_int(union MagicValue *p, const struct MagicLine *m, int bits)
{
  uint64_t u, um;
  int64_t  s, sm;
  int      ok;

  switch (bits) {
    case 8:
      u = p->b;
      break;
    case 16:
      u = p->h;
      break;
    case 32:
      u = p->l;
      break;
    default:
      u = p->q;
      break;
  }
  if (m->flag & MF_UNSIGNED) {
    um = bits == 64 ? m->mask : m->mask & (((uint64_t)1 << bits) - 1);
    ok = domask_u(m, &u, um);
  } else {
    s  = sext(1, bits, u);
    sm = sext(1, bits, m->mask);
    ok = domask_s(m, &s, sm, bits == 64 ? INT64_MIN : -((int64_t)1 << (bits - 1)));
    u  = (uint64_t)s;
  }
  switch (bits) {
    case 8:
      p->b = (uint8_t)u;
      break;
    case 16:
      p->h = (uint16_t)u;
      break;
    case 32:
      p->l = (uint32_t)u;
      break;
    default:
      p->q = u;
      break;
  }
  return ok;
}

static int
cvt_float(union MagicValue *p, const struct MagicLine *m, int dbl)
{
  double v = dbl ? p->d : (double)p->f, x = dbl ? (double)m->mask : (double)(float)m->mask;

  if (!m->mask)
    return 1;
  switch (m->mask_op & OP_MASK) {
    case OP_ADD:
      v += x;
      break;
    case OP_MINUS:
      v -= x;
      break;
    case OP_MULTIPLY:
      v *= x;
      break;
    case OP_DIVIDE:
      if (x == 0)
        return 0;
      v /= x;
      break;
    default:
      return 1;
  }
  if (dbl)
    p->d = v;
  else
    p->f = (float)v;
  return 1;
}

/* put the fetched bytes in host order and apply the mask, 0 when it cant */
static int
mconvert(struct MagicRun *r, const struct MagicLine *m, int flip)
{
  union MagicValue *p = &r->val;
  size_t            sz, len;
  char             *d;
  int               ok;

  switch (flip_type(m->type, flip)) {
    case MT_BYTE:
      return cvt_int(p, m, 8);
    case MT_SHORT:
    case MT_MSDOSDATE:
    case MT_MSDOSTIME:
      return cvt_int(p, m, 16);
    case MT_LONG:
    case MT_DATE:
    case MT_LDATE:
      return cvt_int(p, m, 32);
    case MT_QUAD:
    case MT_QDATE:
    case MT_QLDATE:
    case MT_QWDATE:
    case MT_OFFSET:
      return cvt_int(p, m, 64);
    case MT_STRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
    case MT_OCTAL:
      p->s[MG_STRLEN - 1] = '\0';
      return 1;
    case MT_PSTRING:
      if (!(sz = mg_pstrlen_size(m->sflags)))
        return 0;
      len = pstr_len(m, p->us, &ok);
      if (!ok)
        return 0;
      if (len >= MG_STRLEN - sz)
        len = MG_STRLEN - sz;
      memmove(p->s, p->s + sz, len);
      d  = p->s + len;
      *d = '\0';
      return 1;
    case MT_BESHORT:
    case MT_BEMSDOSDATE:
    case MT_BEMSDOSTIME:
      p->h = be16(p->hs);
      return cvt_int(p, m, 16);
    case MT_BELONG:
    case MT_BEDATE:
    case MT_BELDATE:
      p->l = be32(p->hl);
      return cvt_int(p, m, 32);
    case MT_BEQUAD:
    case MT_BEQDATE:
    case MT_BEQLDATE:
    case MT_BEQWDATE:
      p->q = be64(p->hq);
      return cvt_int(p, m, 64);
    case MT_LESHORT:
    case MT_LEMSDOSDATE:
    case MT_LEMSDOSTIME:
      p->h = le16(p->hs);
      return cvt_int(p, m, 16);
    case MT_LELONG:
    case MT_LEDATE:
    case MT_LELDATE:
      p->l = le32(p->hl);
      return cvt_int(p, m, 32);
    case MT_LEQUAD:
    case MT_LEQDATE:
    case MT_LEQLDATE:
    case MT_LEQWDATE:
      p->q = le64(p->hq);
      return cvt_int(p, m, 64);
    case MT_MELONG:
    case MT_MEDATE:
    case MT_MELDATE:
      p->l = me32(p->hl);
      return cvt_int(p, m, 32);
    case MT_FLOAT:
      return cvt_float(p, m, 0);
    case MT_BEFLOAT:
      p->l = be32(p->hl);
      return cvt_float(p, m, 0);
    case MT_LEFLOAT:
      p->l = le32(p->hl);
      return cvt_float(p, m, 0);
    case MT_DOUBLE:
      return cvt_float(p, m, 1);
    case MT_BEDOUBLE:
      p->q = be64(p->hq);
      return cvt_float(p, m, 1);
    case MT_LEDOUBLE:
      p->q = le64(p->hq);
      return cvt_float(p, m, 1);
    case MT_REGEX:
    case MT_SEARCH:
    case MT_DEFAULT:
    case MT_CLEAR:
    case MT_NAME:
    case MT_USE:
    case MT_DER:
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      return 1;
    default:
      /* varints and unknown types: file(1) cant convert them either */
      return 0;
  }
}

/* apply the indirection operator, 1 when the result is out of range */
static int
do_ops(const struct MagicLine *m, uint32_t *rv, int64_t lhs, int64_t off)
{
  int64_t offset = lhs;

  if (lhs >= (int64_t)UINT_MAX || lhs <= INT_MIN || off >= (int64_t)UINT_MAX || off <= INT_MIN)
    return 1;
  if (off) {
    switch (m->in_op & OP_MASK) {
      case OP_AND:
        offset = lhs & off;
        break;
      case OP_OR:
        offset = lhs | off;
        break;
      case OP_XOR:
        offset = lhs ^ off;
        break;
      case OP_ADD:
        offset = lhs + off;
        break;
      case OP_MINUS:
        offset = lhs - off;
        break;
      case OP_MULTIPLY:
        offset = lhs * off;
        break;
      case OP_DIVIDE:
        offset = lhs / off;
        break;
      case OP_MODULO:
        offset = lhs % off;
        break;
    }
  }
  if (m->in_op & OP_INVERSE)
    offset = ~offset;
  if (offset >= (int64_t)UINT_MAX)
    return 1;
  *rv = (uint32_t)offset;
  return 0;
}

/* read the value an indirect offset points at, 0 when out of range */
static int
read_indir(const unsigned char *s, size_t nbytes, int64_t at, int type, int sgn, int64_t *out)
{
  const unsigned char *hb;
  uint16_t             hs;
  uint32_t             hl;
  uint64_t             v;

  switch (type) {
    case MT_BYTE:
      if (offset_oob(nbytes, at, 1))
        return 0;
      *out = sext(sgn, 8, s[at]);
      return 1;
    case MT_SHORT:
    case MT_BESHORT:
    case MT_LESHORT:
      if (offset_oob(nbytes, at, 2))
        return 0;
      hb = s + at;
      if (type == MT_SHORT) {
        memcpy(&hs, hb, 2);
        v = hs;
      } else {
        v = type == MT_BESHORT ? be16(hb) : le16(hb);
      }
      *out = sext(sgn, 16, v);
      return 1;
    case MT_LONG:
    case MT_BELONG:
    case MT_BEID3:
    case MT_LELONG:
    case MT_LEID3:
    case MT_MELONG:
      if (offset_oob(nbytes, at, 4))
        return 0;
      hb = s + at;
      if (type == MT_LONG) {
        memcpy(&hl, hb, 4);
        v = hl;
      } else if (type == MT_BELONG || type == MT_BEID3) {
        v = be32(hb);
      } else if (type == MT_MELONG) {
        v = me32(hb);
      } else {
        v = le32(hb);
      }
      *out = sext(sgn, 32, v);
      return 1;
    case MT_BEQUAD:
    case MT_LEQUAD:
      if (offset_oob(nbytes, at, 8))
        return 0;
      *out = (int64_t)(type == MT_BEQUAD ? be64(s + at) : le64(s + at));
      return 1;
    default:
      return 0;
  }
}

static uint32_t
cvt_id3(uint32_t v)
{
  return ((v >> 0) & 0x7f) << 0 | ((v >> 8) & 0x7f) << 7 | ((v >> 16) & 0x7f) << 14
         | ((v >> 24) & 0x7f) << 21;
}

static int
indir_lhs(
    struct MagicRun        *r,
    const struct MagicLine *m,
    size_t                  nbytes,
    uint32_t                offset,
    int                     in_type,
    int                     sgn,
    int64_t                *lhs
)
{
  union MagicValue *p = &r->val;
  uint64_t          v;
  uint16_t          hs;
  uint32_t          hl;

  switch (in_type) {
    case MT_BYTE:
      if (offset_oob(nbytes, offset, 1))
        return 0;
      *lhs = sext(sgn, 8, p->b);
      return 1;
    case MT_BESHORT:
    case MT_LESHORT:
    case MT_SHORT:
      if (offset_oob(nbytes, offset, 2))
        return 0;
      if (in_type == MT_SHORT) {
        memcpy(&hs, p->hs, 2);
        v = hs;
      } else {
        v = in_type == MT_BESHORT ? be16(p->hs) : le16(p->hs);
      }
      *lhs = sext(sgn, 16, v);
      return 1;
    case MT_BELONG:
    case MT_BEID3:
    case MT_LELONG:
    case MT_LEID3:
    case MT_MELONG:
    case MT_LONG:
      if (offset_oob(nbytes, offset, 4))
        return 0;
      if (in_type == MT_LONG) {
        memcpy(&hl, p->hl, 4);
        v = hl;
      } else if (in_type == MT_BELONG || in_type == MT_BEID3) {
        v = be32(p->hl);
      } else if (in_type == MT_MELONG) {
        v = me32(p->hl);
      } else {
        v = le32(p->hl);
      }
      if (in_type == MT_BEID3 || in_type == MT_LEID3)
        v = cvt_id3((uint32_t)v);
      *lhs = sext(sgn, 32, v);
      return 1;
    case MT_LEQUAD:
    case MT_BEQUAD:
      if (offset_oob(nbytes, offset, 8))
        return 0;
      *lhs = (int64_t)(in_type == MT_LEQUAD ? le64(p->hq) : be64(p->hq));
      return 1;
    case MT_OCTAL:
      if (offset_oob(nbytes, offset, m->vallen))
        return 0;
      *lhs = (int64_t)strtoull(p->s, NULL, 8);
      return 1;
    default:
      return 0;
  }
}

/* swap the output buffer for a fresh one, as indirect needs */
static void
out_push(struct MagicRun *r, struct MagicOut *save)
{
  *save = r->out;
  memset(&r->out, 0, sizeof(r->out));
}

static void
out_pop(struct MagicRun *r, struct MagicOut *save, struct MagicOut *got)
{
  *got   = r->out;
  r->out = *save;
  if (got->err)
    r->out.err = 1;
}

static void fmt_value(struct MagicOut *, const char *, int, int64_t, double, const char *);

int
mg_find(struct Magic *mg, const char *name, struct MagicLine **v, size_t *n)
{
  struct MagicDb   *db;
  struct MagicLine *ma;
  size_t            i, j;

  for (db = mg->db; db; db = db->next) {
    ma = db->v[1];
    for (i = 0; i < db->n[1]; i++) {
      if (ma[i].type != MT_NAME || strcmp(ma[i].value.s, name) != 0)
        continue;
      for (j = i + 1; j < db->n[1] && ma[j].cont; j++)
        ;
      *v = &ma[i];
      *n = j - i;
      return 0;
    }
  }
  return -1;
}

static int
mget_indirect(
    struct MagicRun     *r,
    struct MagicLine    *m,
    const unsigned char *s,
    size_t               nbytes,
    uint32_t             offset,
    size_t               o,
    int                  text,
    struct Shared       *sh
)
{
  struct MagicOut save, got;
  struct MagicBuf sbb;
  struct MagicDb *db;
  int             rv = -1;

  if (m->sflags & SF_INDIRECT_REL)
    offset += (uint32_t)o;
  if (offset == 0 || nbytes < offset)
    return 0;
  r->nindir++;
  sbb.p    = s + offset;
  sbb.n    = nbytes - offset;
  sbb.file = 0;
  out_push(r, &save);
  for (db = r->m->db; db; db = db->next) {
    if ((rv = match(r, db->v[0], db->n[0], &sbb, 0, MF_BINTEST, text, 0, sh, NULL, NULL)) != 0)
      break;
  }
  out_pop(r, &save, &got);
  if (rv == 1) {
    if (!r->mime)
      fmt_value(&r->out, m->desc, VK_UINT, offset, 0, NULL);
    if (got.s)
      mg_add(&r->out, got.s, got.n);
  }
  free(got.s);
  return rv;
}

static int
mget_use(
    struct MagicRun       *r,
    struct MagicLine      *m,
    const struct MagicBuf *b,
    uint32_t               offset,
    size_t                 o,
    int                    mode,
    int                    text,
    int                    flip,
    struct Shared         *sh,
    int                   *retp,
    int                   *foundp
)
{
  struct MagicLine *ml;
  const char       *name = m->value.s;
  int32_t          *lioff, eoffset;
  int              *ligot;
  size_t            nml;
  int               rv, oneed, nfound = 0;

  if (*name == '^') {
    name++;
    flip = !flip;
  }
  if (mg_find(r->m, name, &ml, &nml) < 0)
    return -1;
  if (!(lioff = malloc(sizeof(r->lioff))))
    return -1;
  if (!(ligot = malloc(sizeof(r->ligot)))) {
    free(lioff);
    return -1;
  }
  memcpy(lioff, r->lioff, sizeof(r->lioff));
  memcpy(ligot, r->ligot, sizeof(r->ligot));
  oneed = sh->need_sep;
  if (m->flag & MF_NOSPACE)
    sh->need_sep = 0;
  r->nnames++;
  eoffset  = r->eoffset;
  rv       = match(r, ml, nml, b, offset + o, mode, text, flip, sh, retp, &nfound);
  r->val.q = (uint64_t)nfound;
  r->nnames--;
  *foundp |= nfound;
  memcpy(r->lioff, lioff, sizeof(r->lioff));
  memcpy(r->ligot, ligot, sizeof(r->ligot));
  free(lioff);
  free(ligot);
  if (rv != 1)
    sh->need_sep = oneed;
  r->offset  = (int32_t)offset;
  r->eoffset = eoffset;
  return rv ? rv : *foundp;
}

/* fetch what test m looks at, 1 when there is something to compare, 0 when not, -1 on error */
static int
mget(
    struct MagicRun       *r,
    struct MagicLine      *m,
    const struct MagicBuf *b,
    const struct MagicBuf *bb,
    size_t                 o,
    unsigned               cont,
    int                    mode,
    int                    text,
    int                    flip,
    struct Shared         *sh,
    int                   *retp,
    int                   *foundp
)
{
  const unsigned char *s      = bb->p;
  size_t               nbytes = bb->n;
  uint32_t             offset = (uint32_t)r->offset;
  int64_t              off, lhs;
  int                  sgn, at;

  if (r->nindir >= MG_INDIR_MAX || r->nnames >= MG_NAME_MAX)
    return -1;
  mcopy(r, m->type, m->flag & MF_INDIR, s, (uint32_t)(offset + o), nbytes, m);
  if (m->flag & MF_INDIR) {
    off = m->in_offset;
    sgn = m->in_op & OP_SIGNED;
    if (m->in_op & OP_INDIRECT) {
      if (offset_oob(nbytes, (int64_t)offset + off, 0))
        return 0;
      at = flip_type(m->in_type, flip);
      if (at == MT_OCTAL) {
        if (offset_oob(nbytes, offset, m->vallen))
          return 0;
        off = (int64_t)strtoull(r->val.s, NULL, 8);
      } else if (!read_indir(s, nbytes, (int64_t)offset + off, at, sgn, &off)) {
        return 0;
      }
    }
    if (!indir_lhs(r, m, nbytes, offset, flip_type(m->in_type, flip), sgn, &lhs))
      return 0;
    if (do_ops(m, &offset, lhs, off))
      return 0;
    if (m->flag & MF_INDIROFFADD) {
      if (cont == 0)
        return 0;
      offset += (uint32_t)r->lioff[cont - 1];
      if (offset == 0)
        return 0;
    }
    mcopy(r, m->type, 0, s, offset, nbytes, m);
    r->offset = (int32_t)offset;
  }
  switch (m->type) {
    case MT_BYTE:
    case MT_SHORT:
    case MT_BESHORT:
    case MT_LESHORT:
    case MT_LONG:
    case MT_BELONG:
    case MT_LELONG:
    case MT_MELONG:
    case MT_DATE:
    case MT_BEDATE:
    case MT_LEDATE:
    case MT_MEDATE:
    case MT_LDATE:
    case MT_BELDATE:
    case MT_LELDATE:
    case MT_MELDATE:
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
    case MT_LEGUID:
    case MT_BEGUID:
    case MT_GUID:
      if (offset_oob(nbytes, offset, mg_typesize(m->type)))
        return 0;
      break;
    case MT_STRING:
    case MT_PSTRING:
    case MT_SEARCH:
    case MT_OCTAL:
      if (offset_oob(nbytes, offset, m->vallen))
        return 0;
      break;
    case MT_REGEX:
      if (nbytes < offset)
        return 0;
      break;
    case MT_INDIRECT:
      return mget_indirect(r, m, s, nbytes, offset, o, text, sh);
    case MT_USE:
      if (nbytes < offset)
        return 0;
      return mget_use(r, m, b, offset, o, mode, text, flip, sh, retp, foundp);
    case MT_NAME:
      if (!r->mime)
        mg_puts(&r->out, m->desc);
      return 1;
    default:
      break;
  }
  return mconvert(r, m, flip);
}

/* compare as file(1) does: a difference, but white space and case flags change the walk */
static uint64_t
strncmp_flags(
    const unsigned char *a, const unsigned char *b, size_t len, size_t maxlen, uint32_t flags
)
{
  const unsigned char *eb;
  uint64_t             v = 0;
  uint32_t             ws;

  ws = flags & (SF_COMPACT_WS | SF_COMPACT_OPT_WS);
  eb = b + (ws ? maxlen : len);
  len++;
  if (!flags) {
    while (--len > 0) {
      if ((v = (uint64_t)((int)*b++ - (int)*a++)) != 0)
        break;
    }
    return v;
  }
  while (--len > 0) {
    if (b >= eb) {
      v = 1;
      break;
    }
    if ((flags & SF_IGNORE_LOWER) && islower(*a)) {
      if ((v = (uint64_t)(tolower(*b) - (int)*a)) != 0)
        break;
      b++;
      a++;
    } else if ((flags & SF_IGNORE_UPPER) && isupper(*a)) {
      if ((v = (uint64_t)(toupper(*b) - (int)*a)) != 0)
        break;
      b++;
      a++;
    } else if ((flags & SF_COMPACT_WS) && isspace(*a)) {
      a++;
      if (isspace(*b)) {
        b++;
        if (!isspace(*a)) {
          while (b < eb && isspace(*b))
            b++;
        }
      } else {
        v = 1;
        break;
      }
    } else if ((flags & SF_COMPACT_OPT_WS) && isspace(*a)) {
      a++;
      while (b < eb && isspace(*b))
        b++;
    } else {
      if ((v = (uint64_t)((int)*b++ - (int)*a++)) != 0)
        break;
    }
  }
  if (len == 0 && v == 0 && (flags & SF_FULL_WORD)) {
    if (*b && !isspace(*b))
      v = 1;
  }
  return v;
}

/* the first match of a needle that starts at or before position max */
static const unsigned char *
find_bytes(const unsigned char *h, size_t hn, const unsigned char *n, size_t nn)
{
  size_t i;

  if (nn > hn)
    return NULL;
  for (i = 0; i + nn <= hn; i++) {
    if (h[i] == n[0] && memcmp(h + i, n, nn) == 0)
      return h + i;
  }
  return NULL;
}

static int
check_search(struct MagicRun *r, const struct MagicLine *m, uint64_t *v)
{
  const unsigned char *found;
  size_t               slen, idx;

  slen = m->vallen < MG_STRLEN ? m->vallen : MG_STRLEN;
  *v   = 0;
  if (slen > 0 && m->sflags == 0) {
    idx = m->range + slen;
    if (m->range == 0 || r->slen < idx)
      idx = r->slen;
    if (!(found = find_bytes(r->ss, idx, m->value.us, slen))) {
      *v = 1;
      return 0;
    }
    idx = (size_t)(found - r->ss);
    r->soff += idx;
    r->rmlen = r->slen - idx;
    return 0;
  }
  for (idx = 0; m->range == 0 || idx < m->range; idx++) {
    if (slen + idx > r->slen) {
      *v = 1;
      break;
    }
    *v = strncmp_flags(m->value.us, r->ss + idx, slen, r->slen - idx, m->sflags);
    if (*v == 0) {
      r->soff += idx;
      r->rmlen = r->slen - idx;
      break;
    }
  }
  return 0;
}

static int
check_regex(struct MagicRun *r, struct MagicLine *m, uint64_t *v)
{
  regmatch_t pm;
  char      *copy;
  size_t     slen = r->slen;
  int        rc;

  if (!m->rx) {
    if (m->rxbad)
      return 0;
    if (!(m->rx = malloc(sizeof(*m->rx))))
      return -1;
    if (regcomp(
            m->rx,
            m->value.s,
            REG_EXTENDED | REG_NEWLINE | (m->sflags & SF_IGNORE_CASE ? REG_ICASE : 0)
        )) {
      free(m->rx);
      m->rx    = NULL;
      m->rxbad = 1;
      return 0;
    }
  }
  /* file(1) hands regexec the window minus its last byte */
  if (!(copy = malloc(slen ? slen : 1)))
    return -1;
  if (slen) {
    memcpy(copy, r->ss, slen);
    copy[--slen] = '\0';
  } else {
    copy[0] = '\0';
  }
  rc = regexec(m->rx, copy, 1, &pm, 0);
  free(copy);
  if (rc == 0) {
    r->ss += pm.rm_so;
    r->soff += (size_t)pm.rm_so;
    r->rmlen = (size_t)(pm.rm_eo - pm.rm_so);
    *v       = 0;
  } else {
    *v = 1;
  }
  return 1;
}

/* does the fetched value pass the test, 1 or 0, -1 on error */
static int
magiccheck(struct MagicRun *r, struct MagicLine *m)
{
  union MagicValue *p = &r->val;
  uint64_t          l = m->value.q, v;
  double            dl, dv;
  int               rc;

  switch (m->type) {
    case MT_BYTE:
      v = p->b;
      break;
    case MT_SHORT:
    case MT_BESHORT:
    case MT_LESHORT:
    case MT_MSDOSDATE:
    case MT_LEMSDOSDATE:
    case MT_BEMSDOSDATE:
    case MT_MSDOSTIME:
    case MT_LEMSDOSTIME:
    case MT_BEMSDOSTIME:
      v = p->h;
      break;
    case MT_LONG:
    case MT_BELONG:
    case MT_LELONG:
    case MT_MELONG:
    case MT_DATE:
    case MT_BEDATE:
    case MT_LEDATE:
    case MT_MEDATE:
    case MT_LDATE:
    case MT_BELDATE:
    case MT_LELDATE:
    case MT_MELDATE:
      v = p->l;
      break;
    case MT_QUAD:
    case MT_LEQUAD:
    case MT_BEQUAD:
    case MT_QDATE:
    case MT_BEQDATE:
    case MT_LEQDATE:
    case MT_QLDATE:
    case MT_BEQLDATE:
    case MT_LEQLDATE:
    case MT_QWDATE:
    case MT_BEQWDATE:
    case MT_LEQWDATE:
    case MT_OFFSET:
      v = p->q;
      break;
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
      if (m->type == MT_FLOAT || m->type == MT_BEFLOAT || m->type == MT_LEFLOAT) {
        dl = m->value.f;
        dv = p->f;
      } else {
        dl = m->value.d;
        dv = p->d;
      }
      switch (m->reln) {
        case 'x':
          return 1;
        case '!':
          return isunordered(dl, dv) ? 1 : dv != dl;
        case '=':
          return isunordered(dl, dv) ? 0 : dv == dl;
        case '>':
          return isgreater(dv, dl);
        case '<':
          return isless(dv, dl);
        default:
          return 0;
      }
    case MT_DEFAULT:
    case MT_CLEAR:
      l = 0;
      v = 0;
      break;
    case MT_STRING:
    case MT_PSTRING:
    case MT_OCTAL:
      l = 0;
      v = strncmp_flags(m->value.us, p->us, m->vallen, MG_STRLEN, m->sflags);
      break;
    case MT_BESTRING16:
    case MT_LESTRING16:
      l = 0;
      v = strncmp_flags(m->value.us, p->us, m->vallen, MG_STRLEN, 0);
      break;
    case MT_SEARCH:
      if (!r->ss)
        return 0;
      l = 0;
      check_search(r, m, &v);
      break;
    case MT_REGEX:
      if (!r->ss)
        return 0;
      l = 0;
      if ((rc = check_regex(r, m, &v)) <= 0)
        return rc;
      break;
    case MT_USE:
      return r->val.q != 0;
    case MT_NAME:
    case MT_INDIRECT:
      return 1;
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      l = 0;
      v = memcmp(m->value.guid, p->guid, 16) != 0;
      break;
    default:
      /* der and anything unknown never match */
      return 0;
  }
  v = mg_signextend(m, v);
  switch (m->reln) {
    case 'x':
      return 1;
    case '!':
      return v != l;
    case '=':
      return v == l;
    case '>':
      if (m->flag & MF_UNSIGNED)
        return v > l;
      return (int64_t)v > (int64_t)l;
    case '<':
      if (m->flag & MF_UNSIGNED)
        return v < l;
      return (int64_t)v < (int64_t)l;
    case '&':
      return (v & l) == l;
    case '^':
      return (v & l) != l;
    default:
      return 0;
  }
}

/* parse the one printf conversion of a description: its flags, width and precision */
struct Conv {
  const char *pre; /* text before the conversion */
  size_t      npre;
  const char *post; /* text after it */
  char        spec[32];
  int         conv; /* the conversion letter, 0 when there is none */
};

static void
conv_parse(const char *desc, struct Conv *c)
{
  const char *p = desc, *q;
  size_t      n = 0;
  int         digits;

  memset(c, 0, sizeof(*c));
  for (;;) {
    if (!(p = strchr(p, '%'))) {
      c->pre  = desc;
      c->npre = strlen(desc);
      c->post = desc + c->npre;
      return;
    }
    if (p[1] != '%')
      break;
    p += 2;
  }
  c->pre       = desc;
  c->npre      = (size_t)(p - desc);
  c->spec[n++] = '%';
  for (q = p + 1; *q && strchr("#0-+ '", *q); q++) {
    if (*q != '\'' && n < 8)
      c->spec[n++] = *q;
  }
  for (digits = 0; isdigit((unsigned char)*q); q++, digits++) {
    if (digits < 4)
      c->spec[n++] = *q;
  }
  if (*q == '.') {
    c->spec[n++] = *q++;
    for (digits = 0; isdigit((unsigned char)*q); q++, digits++) {
      if (digits < 4)
        c->spec[n++] = *q;
    }
  }
  while (*q && strchr("hlLqjzt", *q))
    q++;
  c->conv    = (unsigned char)*q;
  c->spec[n] = '\0';
  c->post    = *q ? q + 1 : q;
}

/* print a description with its value, the way file(1) feeds both to printf */
static void
fmt_value(struct MagicOut *o, const char *desc, int kind, int64_t iv, double dv, const char *sv)
{
  struct Conv c;
  char        spec[40], num[64], *buf;
  int         len;

  conv_parse(desc, &c);
  mg_add(o, c.pre, c.npre);
  if (!c.conv) {
    mg_puts(o, c.post);
    return;
  }
  if (c.conv == 's' && kind != VK_STR) {
    if (kind == VK_FLOAT)
      snprintf(num, sizeof(num), "%g", dv);
    else if (kind == VK_QUAD)
      snprintf(num, sizeof(num), "%lld", (long long)iv);
    else if (kind == VK_UQUAD)
      snprintf(num, sizeof(num), "%llu", (unsigned long long)iv);
    else if (kind == VK_UINT)
      snprintf(num, sizeof(num), "%u", (unsigned)iv);
    else
      snprintf(num, sizeof(num), "%d", (int)iv);
    sv   = num;
    kind = VK_STR;
  }
  switch (kind) {
    case VK_STR:
      snprintf(spec, sizeof(spec), "%ss", c.spec);
      if (c.conv != 's')
        strcpy(spec, "%s");
      len = snprintf(NULL, 0, spec, sv);
      break;
    case VK_FLOAT:
      snprintf(spec, sizeof(spec), "%s%c", c.spec, strchr("eEfFgG", c.conv) ? c.conv : 'g');
      len = snprintf(NULL, 0, spec, dv);
      break;
    case VK_QUAD:
    case VK_UQUAD:
      if (strchr("diouxX", c.conv))
        snprintf(spec, sizeof(spec), "%sll%c", c.spec, c.conv);
      else
        strcpy(spec, kind == VK_UQUAD ? "%llu" : "%lld");
      len = snprintf(NULL, 0, spec, (long long)iv);
      break;
    default:
      if (strchr("diouxXc", c.conv))
        snprintf(spec, sizeof(spec), "%s%c", c.spec, c.conv);
      else
        strcpy(spec, kind == VK_UINT ? "%u" : "%d");
      len = snprintf(NULL, 0, spec, (int)iv);
      break;
  }
  if (len < 0 || !(buf = malloc((size_t)len + 1))) {
    o->err = 1;
    return;
  }
  switch (kind) {
    case VK_STR:
      snprintf(buf, (size_t)len + 1, spec, sv);
      break;
    case VK_FLOAT:
      snprintf(buf, (size_t)len + 1, spec, dv);
      break;
    case VK_QUAD:
    case VK_UQUAD:
      snprintf(buf, (size_t)len + 1, spec, (long long)iv);
      break;
    default:
      if (strchr("ouxX", spec[strlen(spec) - 1]))
        snprintf(buf, (size_t)len + 1, spec, (unsigned)(int)iv);
      else
        snprintf(buf, (size_t)len + 1, spec, (int)iv);
      break;
  }
  mg_add(o, buf, (size_t)len);
  free(buf);
  mg_puts(o, c.post);
}

static char *
strtrim(char *s)
{
  char *last;

  while (isspace((unsigned char)*s))
    s++;
  for (last = s; *last; last++)
    ;
  if (last == s)
    return s;
  for (--last; isspace((unsigned char)*last); last--)
    ;
  *++last = '\0';
  return s;
}

static const char *
fmt_datetime(char *buf, size_t n, int64_t t, int local)
{
  static const char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char *mon[]  = {
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  struct tm tm, *tp;
  time_t    tt;

  if (t > MAX_CTIME || (sizeof(time_t) < 8 && (t > INT32_MAX || t < INT32_MIN)))
    goto bad;
  tt = (time_t)t;
  if (local) {
    tzset();
    tp = localtime_r(&tt, &tm);
  } else {
    tp = gmtime_r(&tt, &tm);
  }
  if (!tp || tm.tm_wday < 0 || tm.tm_wday > 6 || tm.tm_mon < 0 || tm.tm_mon > 11)
    goto bad;
  snprintf(
      buf,
      n,
      "%.3s %.3s%3d %.2d:%.2d:%.2d %d",
      wday[tm.tm_wday],
      mon[tm.tm_mon],
      tm.tm_mday,
      tm.tm_hour,
      tm.tm_min,
      tm.tm_sec,
      1900 + tm.tm_year
  );
  return buf;
bad:
  snprintf(buf, n, "*Invalid datetime*");
  return buf;
}

static const char *
fmt_dosdate(char *buf, size_t n, uint16_t v)
{
  struct tm tm;

  memset(&tm, 0, sizeof(tm));
  tm.tm_mday = v & 0x1f;
  tm.tm_mon  = ((v >> 5) & 0xf) - 1;
  if (tm.tm_mon < 0 || tm.tm_mon > 11)
    tm.tm_mon = 0;
  tm.tm_year = (v >> 9) + 80;
  if (!strftime(buf, n, "%b %d %Y", &tm))
    snprintf(buf, n, "*Invalid date*");
  return buf;
}

static const char *
fmt_dostime(char *buf, size_t n, uint16_t v)
{
  struct tm tm;

  memset(&tm, 0, sizeof(tm));
  tm.tm_sec  = (v & 0x1f) * 2;
  tm.tm_min  = (v >> 5) & 0x3f;
  tm.tm_hour = v >> 11;
  if (!strftime(buf, n, "%H:%M:%S", &tm))
    snprintf(buf, n, "*Invalid time*");
  return buf;
}

static const char *
fmt_guid(char *buf, size_t n, const unsigned char *g, int be)
{
  snprintf(
      buf,
      n,
      "%.8lX-%.4X-%.4X-%.2X%.2X-%.2X%.2X%.2X%.2X%.2X%.2X",
      (unsigned long)(be ? be32(g) : le32(g)),
      be ? be16(g + 4) : le16(g + 4),
      be ? be16(g + 6) : le16(g + 6),
      g[8],
      g[9],
      g[10],
      g[11],
      g[12],
      g[13],
      g[14],
      g[15]
  );
  return buf;
}

/* print the description of a matched line with its value */
static void
mprint(struct MagicRun *r, struct MagicLine *m)
{
  union MagicValue  *p = &r->val;
  struct MagicOut   *o = &r->out;
  unsigned long long u;
  char               ebuf[SBUFLEN], sbuf[SBUFLEN], tbuf[64], *str, *cp;
  const char        *desc = m->desc;
  int64_t            v;
  int                unsig = m->flag & MF_UNSIGNED;

  if (mg_varexpand(ebuf, sizeof(ebuf), m->desc, r->xmode) == 0)
    desc = ebuf;
  switch (m->type) {
    case MT_BYTE:
      v = (int64_t)mg_signextend(m, p->b);
      fmt_value(
          o,
          desc,
          unsig ? VK_UINT : VK_INT,
          unsig ? (int64_t)(uint8_t)v : (int64_t)(int8_t)v,
          0,
          NULL
      );
      break;
    case MT_SHORT:
    case MT_BESHORT:
    case MT_LESHORT:
      v = (int64_t)mg_signextend(m, p->h);
      fmt_value(
          o,
          desc,
          unsig ? VK_UINT : VK_INT,
          unsig ? (int64_t)(uint16_t)v : (int64_t)(int16_t)v,
          0,
          NULL
      );
      break;
    case MT_LONG:
    case MT_BELONG:
    case MT_LELONG:
    case MT_MELONG:
      v = (int64_t)mg_signextend(m, p->l);
      fmt_value(
          o,
          desc,
          unsig ? VK_UINT : VK_INT,
          unsig ? (int64_t)(uint32_t)v : (int64_t)(int32_t)v,
          0,
          NULL
      );
      break;
    case MT_QUAD:
    case MT_BEQUAD:
    case MT_LEQUAD:
    case MT_OFFSET:
      v = (int64_t)mg_signextend(m, p->q);
      fmt_value(o, desc, unsig ? VK_UQUAD : VK_QUAD, v, 0, NULL);
      break;
    case MT_STRING:
    case MT_PSTRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
      if (m->reln == '=' || m->reln == '!') {
        mg_printable(sbuf, sizeof(sbuf), m->value.s, MG_STRLEN);
      } else {
        str = p->s;
        if (!m->value.s[0])
          str[strcspn(str, "\r\n")] = '\0';
        if (m->sflags & SF_TRIM)
          str = strtrim(str);
        mg_printable(sbuf, sizeof(sbuf), str, MG_STRLEN - (size_t)(str - p->s));
      }
      fmt_value(o, desc, VK_STR, 0, 0, sbuf);
      break;
    case MT_DATE:
    case MT_BEDATE:
    case MT_LEDATE:
    case MT_MEDATE:
    case MT_LDATE:
    case MT_BELDATE:
    case MT_LELDATE:
    case MT_MELDATE:
      fmt_datetime(
          tbuf,
          sizeof(tbuf),
          p->l,
          m->type == MT_LDATE || m->type == MT_BELDATE || m->type == MT_LELDATE
              || m->type == MT_MELDATE
      );
      fmt_value(o, desc, VK_STR, 0, 0, tbuf);
      break;
    case MT_QDATE:
    case MT_BEQDATE:
    case MT_LEQDATE:
    case MT_QLDATE:
    case MT_BEQLDATE:
    case MT_LEQLDATE:
      fmt_datetime(
          tbuf,
          sizeof(tbuf),
          (int64_t)p->q,
          m->type == MT_QLDATE || m->type == MT_BEQLDATE || m->type == MT_LEQLDATE
      );
      fmt_value(o, desc, VK_STR, 0, 0, tbuf);
      break;
    case MT_QWDATE:
    case MT_BEQWDATE:
    case MT_LEQWDATE:
      fmt_datetime(tbuf, sizeof(tbuf), (int64_t)(p->q / 10000000) - WIN_EPOCH, 0);
      fmt_value(o, desc, VK_STR, 0, 0, tbuf);
      break;
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
      fmt_value(o, desc, VK_FLOAT, 0, p->f, NULL);
      break;
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
      fmt_value(o, desc, VK_FLOAT, 0, p->d, NULL);
      break;
    case MT_SEARCH:
    case MT_REGEX:
      if (!r->ss)
        break;
      if (!(cp = malloc(r->rmlen + 1))) {
        o->err = 1;
        break;
      }
      memcpy(cp, r->ss, r->rmlen);
      cp[r->rmlen] = '\0';
      str          = (m->sflags & SF_TRIM) ? strtrim(cp) : cp;
      mg_printable(sbuf, sizeof(sbuf), str, r->rmlen);
      free(cp);
      fmt_value(o, desc, VK_STR, 0, 0, sbuf);
      break;
    case MT_DEFAULT:
    case MT_CLEAR:
      mg_puts(o, m->desc);
      break;
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      fmt_value(o, desc, VK_STR, 0, 0, fmt_guid(tbuf, sizeof(tbuf), p->guid, m->type == MT_BEGUID));
      break;
    case MT_MSDOSDATE:
    case MT_BEMSDOSDATE:
    case MT_LEMSDOSDATE:
      fmt_value(o, desc, VK_STR, 0, 0, fmt_dosdate(tbuf, sizeof(tbuf), p->h));
      break;
    case MT_MSDOSTIME:
    case MT_BEMSDOSTIME:
    case MT_LEMSDOSTIME:
      fmt_value(o, desc, VK_STR, 0, 0, fmt_dostime(tbuf, sizeof(tbuf), p->h));
      break;
    case MT_OCTAL:
      u = strtoull(m->value.s, &str, 8);
      if (*str)
        snprintf(tbuf, sizeof(tbuf), "*Invalid number*");
      else
        snprintf(tbuf, sizeof(tbuf), "%llu", u);
      fmt_value(o, desc, VK_STR, 0, 0, tbuf);
      break;
    default:
      break;
  }
}

/* where the next relative offset starts, 1 when in range */
static int
moffset(struct MagicRun *r, const struct MagicLine *m, size_t nbytes, size_t offset, int32_t *op)
{
  union MagicValue *p = &r->val;
  int32_t           o;
  size_t            vlen;

  switch (m->type) {
    case MT_BYTE:
      o = r->offset + 1;
      break;
    case MT_SHORT:
    case MT_BESHORT:
    case MT_LESHORT:
    case MT_MSDOSDATE:
    case MT_LEMSDOSDATE:
    case MT_BEMSDOSDATE:
    case MT_MSDOSTIME:
    case MT_LEMSDOSTIME:
    case MT_BEMSDOSTIME:
      o = r->offset + 2;
      break;
    case MT_LONG:
    case MT_BELONG:
    case MT_LELONG:
    case MT_MELONG:
    case MT_DATE:
    case MT_BEDATE:
    case MT_LEDATE:
    case MT_MEDATE:
    case MT_LDATE:
    case MT_BELDATE:
    case MT_LELDATE:
    case MT_MELDATE:
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
      o = r->offset + 4;
      break;
    case MT_QUAD:
    case MT_BEQUAD:
    case MT_LEQUAD:
    case MT_QDATE:
    case MT_BEQDATE:
    case MT_LEQDATE:
    case MT_QLDATE:
    case MT_BEQLDATE:
    case MT_LEQLDATE:
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
      o = r->offset + 8;
      break;
    case MT_STRING:
    case MT_PSTRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
    case MT_OCTAL:
      if (m->reln == '=' || m->reln == '!') {
        o = r->offset + m->vallen;
      } else {
        if (!m->value.s[0])
          p->s[strcspn(p->s, "\r\n")] = '\0';
        o = (int32_t)((uint32_t)r->offset + (uint32_t)strlen(p->s));
        if (m->type == MT_PSTRING)
          o += (int32_t)mg_pstrlen_size(m->sflags);
      }
      break;
    case MT_REGEX:
      vlen = (m->sflags & SF_REGEX_START) ? 0 : r->rmlen;
      o    = (int32_t)(r->soff + vlen - offset);
      break;
    case MT_SEARCH:
      vlen = (m->sflags & SF_REGEX_START) ? 0 : m->vallen;
      o    = (int32_t)(r->soff + vlen - offset);
      break;
    case MT_CLEAR:
    case MT_DEFAULT:
    case MT_INDIRECT:
    case MT_OFFSET:
    case MT_USE:
      o = r->offset;
      break;
    case MT_DER:
      *op = 0;
      return 0;
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      o = r->offset + 16;
      break;
    default:
      o = 0;
      break;
  }
  if ((size_t)(uint32_t)o > nbytes || o < 0)
    return -1;
  *op = o;
  return 1;
}

/* string tests may be restricted to text or binary files */
static int
skip_line(const struct MagicLine *m, int mode, int text)
{
  uint32_t f;

  if (m->type == MT_NAME)
    return 0;
  if (mg_isstring(m->type)) {
    f = m->sflags & (SF_BINTEST | SF_TEXTTEST);
    if ((text && f == SF_BINTEST) || (!text && f == SF_TEXTTEST))
      return 1;
  }
  return (m->flag & mode) != mode;
}

static int
annotate(struct MagicRun *r, const struct MagicLine *m, int firstline)
{
  char buf[1024];

  if (!r->mime || !m->mime[0])
    return 0;
  if (!firstline)
    return 1;
  if (mg_varexpand(buf, sizeof(buf), m->mime, r->xmode) == 0)
    mg_puts(&r->out, buf);
  else
    mg_puts(&r->out, m->mime);
  return 1;
}

static size_t
skip_conts(const struct MagicLine *v, size_t nv, size_t i)
{
  while (i + 1 < nv && v[i + 1].cont)
    i++;
  return i;
}

/*
 * run the tests in v against b: the first top level test that matches
 * prints its description, then every continuation whose parents
 * matched prints its own. returns 1 if something was printed
 */
static int
match(
    struct MagicRun       *r,
    struct MagicLine      *v,
    size_t                 nv,
    const struct MagicBuf *b,
    size_t                 o,
    int                    mode,
    int                    text,
    int                    flip,
    struct Shared         *sh,
    int                   *retp,
    int                   *foundp
)
{
  struct MagicLine *m;
  struct MagicBuf   bb;
  size_t            i;
  unsigned          cont = 0;
  int               retv = 0, foundv = 0, flush, e, rc;

  if (!retp)
    retp = &retv;
  if (!foundp)
    foundp = &foundv;
  memset(&bb, 0, sizeof(bb));
  r->ligot[0] = 0;
  for (i = 0; i < nv; i++) {
    m = &v[i];
    if (skip_line(m, mode, text))
      goto flush;
    if (setoffset(r, m, &bb, b, o, cont) < 0) {
      if (r->tstate < 0 && b->file)
        continue;
      goto flush;
    }
    switch (mget(r, m, b, &bb, o, cont, mode, text, flip, sh, retp, foundp)) {
      case -1:
        return -1;
      case 0:
        flush = m->reln != '!';
        break;
      default:
        if (m->type == MT_INDIRECT) {
          *foundp = 1;
          *retp   = 1;
        }
        if ((rc = magiccheck(r, m)) < 0)
          return -1;
        flush = !rc;
        break;
    }
    if (flush)
      goto flush;
    if ((e = annotate(r, m, sh->firstline)) != 0) {
      *foundp = sh->need_sep = sh->printed = *retp = 1;
      sh->firstline                                = 0;
      return e;
    }
    if (m->desc[0]) {
      *foundp = 1;
      if (!r->mime) {
        *retp = sh->need_sep = sh->printed = 1;
        mprint(r, m);
      }
    }
    if (moffset(r, m, bb.n, o, &r->lioff[cont]) <= 0)
      goto flush;
    if (++cont >= MG_LEVELS)
      return -1;
    r->ligot[cont] = 0;
    while (i + 1 < nv && v[i + 1].cont) {
      m = &v[++i];
      if (cont < m->cont)
        continue;
      if (cont > m->cont)
        cont = m->cont;
      if (setoffset(r, m, &bb, b, o, cont) < 0) {
        if (r->tstate < 0 && b->file)
          continue;
        goto flush;
      }
      if (m->flag & MF_OFFADD) {
        if (cont == 0)
          return 0;
        r->offset += r->lioff[cont - 1];
      }
      switch (mget(r, m, b, &bb, o, cont, mode, text, flip, sh, retp, foundp)) {
        case -1:
          return -1;
        case 0:
          if (m->reln != '!')
            continue;
          flush = 1;
          break;
        default:
          if (m->type == MT_INDIRECT) {
            *foundp = 1;
            *retp   = 1;
          }
          flush = 0;
          break;
      }
      if ((rc = flush ? 1 : magiccheck(r, m)) < 0)
        return -1;
      if (!rc)
        continue;
      if (m->type == MT_CLEAR) {
        r->ligot[cont] = 0;
      } else if (r->ligot[cont]) {
        if (m->type == MT_DEFAULT)
          continue;
      } else {
        r->ligot[cont] = 1;
      }
      if ((e = annotate(r, m, sh->firstline)) != 0) {
        *foundp = sh->need_sep = sh->printed = *retp = 1;
        sh->firstline                                = 0;
        return e;
      }
      if (m->desc[0])
        *foundp = 1;
      if (!r->mime && m->desc[0]) {
        *retp       = 1;
        sh->printed = 1;
        if (sh->need_sep && !(m->flag & MF_NOSPACE))
          mg_puts(&r->out, " ");
        mprint(r, m);
        sh->need_sep = 1;
      }
      if (moffset(r, m, bb.n, o, &r->lioff[cont]) <= 0)
        cont--;
      if (++cont >= MG_LEVELS)
        return -1;
      r->ligot[cont] = 0;
    }
    if (sh->printed)
      sh->firstline = 0;
    if (*foundp)
      return *retp;
    cont = 0;
    continue;
  flush:
    i    = skip_conts(v, nv, i);
    cont = 0;
  }
  return *retp;
}

/*
 * run every loaded database against b, the binary tests when mode is
 * MF_BINTEST and the text tests when it is MF_TEXTTEST. text tells if
 * the bytes looked like text. returns 1 when something matched
 */
int
mg_softmagic(struct MagicRun *r, const struct MagicBuf *b, int mode, int text)
{
  struct MagicDb *db;
  struct Shared   sh;
  int             ret;

  sh.printed   = 0;
  sh.need_sep  = 0;
  sh.firstline = 1;
  r->nindir    = 0;
  r->nnames    = 0;
  for (db = r->m->db; db; db = db->next) {
    if ((ret = match(r, db->v[0], db->n[0], b, 0, mode, text, 0, &sh, NULL, NULL)) != 0)
      return ret;
  }
  return 0;
}
