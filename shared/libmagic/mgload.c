/* see LICENSE file for copyright and license details */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mgint.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MULT 10

/* what a printf format in a description must look like for each type */
enum TypeFmt { FMT_NONE, FMT_NUM, FMT_STR, FMT_QUAD, FMT_FLOAT };

struct TypeName {
  const char   *name;
  unsigned char fmt;
};

/* indexed by enum MagicType, scanned in this order for prefix matches */
static const struct TypeName types[MT_NTYPES] = {
    {"invalid", FMT_NONE},    {"byte", FMT_NUM},       {"short", FMT_NUM},
    {"default", FMT_NONE},    {"long", FMT_NUM},       {"string", FMT_STR},
    {"date", FMT_STR},        {"beshort", FMT_NUM},    {"belong", FMT_NUM},
    {"bedate", FMT_STR},      {"leshort", FMT_NUM},    {"lelong", FMT_NUM},
    {"ledate", FMT_STR},      {"pstring", FMT_STR},    {"ldate", FMT_STR},
    {"beldate", FMT_STR},     {"leldate", FMT_STR},    {"regex", FMT_STR},
    {"bestring16", FMT_STR},  {"lestring16", FMT_STR}, {"search", FMT_STR},
    {"medate", FMT_STR},      {"meldate", FMT_STR},    {"melong", FMT_NUM},
    {"quad", FMT_QUAD},       {"lequad", FMT_QUAD},    {"bequad", FMT_QUAD},
    {"qdate", FMT_STR},       {"leqdate", FMT_STR},    {"beqdate", FMT_STR},
    {"qldate", FMT_STR},      {"leqldate", FMT_STR},   {"beqldate", FMT_STR},
    {"float", FMT_FLOAT},     {"befloat", FMT_FLOAT},  {"lefloat", FMT_FLOAT},
    {"double", FMT_FLOAT},    {"bedouble", FMT_FLOAT}, {"ledouble", FMT_FLOAT},
    {"beid3", FMT_NUM},       {"leid3", FMT_NUM},      {"indirect", FMT_NUM},
    {"qwdate", FMT_STR},      {"leqwdate", FMT_STR},   {"beqwdate", FMT_STR},
    {"name", FMT_NONE},       {"use", FMT_NONE},       {"clear", FMT_NONE},
    {"der", FMT_STR},         {"guid", FMT_STR},       {"leguid", FMT_STR},
    {"beguid", FMT_STR},      {"offset", FMT_QUAD},    {"bevarint", FMT_STR},
    {"levarint", FMT_STR},    {"msdosdate", FMT_STR},  {"lemsdosdate", FMT_STR},
    {"bemsdosdate", FMT_STR}, {"msdostime", FMT_STR},  {"lemsdostime", FMT_STR},
    {"bemsdostime", FMT_STR}, {"octal", FMT_STR},
};

/* text sources: a test and its continuations, kept together while sorting */
struct Group {
  struct MagicLine *v;
  size_t            n;
  size_t            cap;
  long              strength;
  char              apple[8];
  char              ext[120];
  unsigned char     img[MG_NEWSIZE];
};

struct GroupSet {
  struct Group *g;
  size_t        n;
  size_t        cap;
};

struct TextLoad {
  struct GroupSet set[MG_SETS];
  struct Group    cur;
  int             skip; /* level 0 line was bad, drop its continuations */
  int             nomem;
  size_t          nbad; /* lines that did not parse */
};

int
mg_isstring(int t)
{
  switch (t) {
    case MT_STRING:
    case MT_PSTRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
    case MT_REGEX:
    case MT_SEARCH:
    case MT_INDIRECT:
    case MT_NAME:
    case MT_USE:
    case MT_OCTAL:
      return 1;
    default:
      return 0;
  }
}

size_t
mg_typesize(int t)
{
  switch (t) {
    case MT_BYTE:
      return 1;
    case MT_SHORT:
    case MT_LESHORT:
    case MT_BESHORT:
    case MT_MSDOSDATE:
    case MT_BEMSDOSDATE:
    case MT_LEMSDOSDATE:
    case MT_MSDOSTIME:
    case MT_BEMSDOSTIME:
    case MT_LEMSDOSTIME:
      return 2;
    case MT_LONG:
    case MT_LELONG:
    case MT_BELONG:
    case MT_MELONG:
    case MT_DATE:
    case MT_LEDATE:
    case MT_BEDATE:
    case MT_MEDATE:
    case MT_LDATE:
    case MT_LELDATE:
    case MT_BELDATE:
    case MT_MELDATE:
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
    case MT_BEID3:
    case MT_LEID3:
      return 4;
    case MT_QUAD:
    case MT_BEQUAD:
    case MT_LEQUAD:
    case MT_QDATE:
    case MT_LEQDATE:
    case MT_BEQDATE:
    case MT_QLDATE:
    case MT_LEQLDATE:
    case MT_BEQLDATE:
    case MT_QWDATE:
    case MT_LEQWDATE:
    case MT_BEQWDATE:
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
    case MT_OFFSET:
    case MT_BEVARINT:
    case MT_LEVARINT:
      return 8;
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      return 16;
    default:
      return 0;
  }
}

void
mg_free(struct MagicDb *db)
{
  struct MagicDb *next;
  size_t          i, j;

  for (; db; db = next) {
    next = db->next;
    for (i = 0; i < MG_SETS; i++) {
      for (j = 0; j < db->n[i]; j++) {
        if (db->v[i][j].rx) {
          regfree(db->v[i][j].rx);
          free(db->v[i][j].rx);
        }
      }
      free(db->v[i]);
    }
    free(db);
  }
}

/* compiled databases: fields are in the byte order of the machine that wrote them */
static uint32_t
get32(const unsigned char *p, int be)
{
  if (be)
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
  return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static uint16_t
get16(const unsigned char *p, int be)
{
  return be ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[1] << 8 | p[0]);
}

static uint64_t
get64(const unsigned char *p, int be)
{
  if (be)
    return (uint64_t)get32(p, 1) << 32 | get32(p + 4, 1);
  return (uint64_t)get32(p + 4, 0) << 32 | get32(p, 0);
}

static void
mgc_decode(struct MagicLine *m, const unsigned char *p, int be, uint32_t version)
{
  uint32_t u32;
  uint64_t u64;
  int      t;

  memset(m, 0, sizeof(*m));
  /* format 20 swapped the widths of flag and cont, 21 inserted leguid and beguid at 50 */
  if (version >= 20) {
    m->flag = get16(p, be);
    m->cont = p[2];
  } else {
    m->cont = (uint8_t)get16(p, be);
    m->flag = p[2];
  }
  m->factor    = p[3];
  m->reln      = p[4];
  m->vallen    = p[5];
  t            = p[6];
  m->in_type   = p[7];
  m->in_op     = p[8];
  m->mask_op   = p[9];
  m->factor_op = p[11];
  if (version < 21) {
    if (t >= MT_LEGUID)
      t += 2;
    if (m->in_type >= MT_LEGUID)
      m->in_type = (uint8_t)(m->in_type + 2);
  }
  m->type      = t < MT_NTYPES ? t : MT_INVALID;
  m->offset    = (int32_t)get32(p + 12, be);
  m->in_offset = (int32_t)get32(p + 16, be);
  m->lineno    = get32(p + 20, be);
  if (mg_isstring(m->type)) {
    m->range  = get32(p + 24, be);
    m->sflags = get32(p + 28, be);
  } else {
    m->mask = get64(p + 24, be);
  }
  switch (m->type) {
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
      u32 = get32(p + 32, be);
      memcpy(&m->value.f, &u32, sizeof(u32));
      break;
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
      u64 = get64(p + 32, be);
      memcpy(&m->value.d, &u64, sizeof(u64));
      break;
    default:
      if (mg_isstring(m->type) || mg_typesize(m->type) == 16 || m->type == MT_DER)
        memcpy(m->value.s, p + 32, MG_STRLEN);
      else
        m->value.q = get64(p + 32, be);
      break;
  }
  memcpy(m->desc, p + 160, MG_DESCLEN);
  m->desc[MG_DESCLEN - 1] = '\0';
  memcpy(m->mime, p + 224, MG_MIMELEN);
  m->mime[MG_MIMELEN - 1] = '\0';
}

static int
readall(const char *path, unsigned char **bufp, size_t *lenp)
{
  struct stat    st;
  unsigned char *buf;
  size_t         len, got;
  ssize_t        r;
  int            fd, e;

  if ((fd = open(path, O_RDONLY)) < 0)
    return -1;
  if (fstat(fd, &st) < 0) {
    e = errno;
    close(fd);
    errno = e;
    return -1;
  }
  if (!S_ISREG(st.st_mode)) {
    close(fd);
    errno = ENOEXEC;
    return -1;
  }
  len = (size_t)st.st_size;
  if (!(buf = malloc(len ? len : 1))) {
    close(fd);
    errno = ENOMEM;
    return -1;
  }
  for (got = 0; got < len; got += (size_t)r) {
    if ((r = read(fd, buf + got, len - got)) <= 0) {
      e = r < 0 ? errno : ENOEXEC;
      free(buf);
      close(fd);
      errno = e;
      return -1;
    }
  }
  close(fd);
  *bufp = buf;
  *lenp = len;
  return 0;
}

static struct MagicDb *
mgc_load(const char *path)
{
  struct MagicDb *db;
  unsigned char  *buf;
  size_t          len, esize, nent, i, j, k;
  uint32_t        version, cnt[MG_SETS];
  int             be;

  if (readall(path, &buf, &len) < 0)
    return NULL;
  if (len < 8)
    goto bad;
  if (get32(buf, 0) == MG_MAGICNO)
    be = 0;
  else if (get32(buf, 1) == MG_MAGICNO)
    be = 1;
  else
    goto bad;
  version = get32(buf + 4, be);
  switch (version) {
    case 16:
    case 18:
      esize = MG_OLDSIZE;
      break;
    case 20:
    case 21:
      esize = MG_NEWSIZE;
      break;
    default:
      goto bad;
  }
  if (len % esize || len / esize < MG_SETS)
    goto bad;
  nent   = len / esize;
  cnt[0] = get32(buf + 8, be);
  cnt[1] = get32(buf + 12, be);
  if (cnt[0] > nent || cnt[1] > nent || (size_t)cnt[0] + cnt[1] + 1 != nent)
    goto bad;
  if (!(db = calloc(1, sizeof(*db))))
    goto nomem;
  for (i = 0, k = 1; i < MG_SETS; i++) {
    if (!cnt[i])
      continue;
    if (!(db->v[i] = calloc(cnt[i], sizeof(*db->v[i])))) {
      mg_free(db);
      goto nomem;
    }
    db->n[i] = cnt[i];
    for (j = 0; j < cnt[i]; j++, k++)
      mgc_decode(&db->v[i][j], buf + k * esize, be, version);
  }
  free(buf);
  return db;
bad:
  free(buf);
  errno = ENOEXEC;
  return NULL;
nomem:
  free(buf);
  errno = ENOMEM;
  return NULL;
}

/* text sources: magic(5) lines, parsed as file(1) parses them when it compiles */
static int
get_op(int c)
{
  switch (c) {
    case '&':
      return OP_AND;
    case '|':
      return OP_OR;
    case '^':
      return OP_XOR;
    case '+':
      return OP_ADD;
    case '-':
      return OP_MINUS;
    case '*':
      return OP_MULTIPLY;
    case '/':
      return OP_DIVIDE;
    case '%':
      return OP_MODULO;
    default:
      return -1;
  }
}

static const char *
eatab(const char *l)
{
  while (*l && (unsigned char)*l < 0x80 && isspace((unsigned char)*l))
    l++;
  return l;
}

static void
eatsize(const char **p)
{
  const char *l = *p;

  if (tolower((unsigned char)*l) == 'u')
    l++;
  switch (tolower((unsigned char)*l)) {
    case 'l':
    case 's':
    case 'h':
    case 'b':
    case 'c':
      l++;
      break;
    default:
      break;
  }
  *p = l;
}

static int
type_keyword(const char *l, const char **end)
{
  size_t len;
  int    i;

  for (i = 1; i < MT_NTYPES; i++) {
    len = strlen(types[i].name);
    if (strncmp(l, types[i].name, len) == 0) {
      *end = l + len;
      return i;
    }
  }
  return MT_INVALID;
}

/* the posix spellings: d, u, d1 to d8, dC, dS, dI, dL, dQ and their u forms */
static int
type_posix(const char *l, const char **end)
{
  int t;

  if (!l[0] || !l[1])
    return MT_INVALID;
  if (isalpha((unsigned char)l[1])) {
    switch (l[1]) {
      case 'C':
        t = MT_BYTE;
        break;
      case 'S':
        t = MT_SHORT;
        break;
      case 'I':
      case 'L':
        t = MT_LONG;
        break;
      case 'Q':
        t = MT_QUAD;
        break;
      default:
        return MT_INVALID;
    }
    l += 2;
  } else if (isdigit((unsigned char)l[1])) {
    if (isdigit((unsigned char)l[2]))
      return MT_INVALID;
    switch (l[1]) {
      case '1':
        t = MT_BYTE;
        break;
      case '2':
        t = MT_SHORT;
        break;
      case '4':
        t = MT_LONG;
        break;
      case '8':
        t = MT_QUAD;
        break;
      default:
        return MT_INVALID;
    }
    l += 2;
  } else {
    t = MT_LONG;
    l++;
  }
  *end = l;
  return t;
}

static int
type_special(const char *l, const char **end)
{
  static const int special[] = {MT_DER, MT_NAME, MT_USE, MT_OCTAL};
  size_t           i, len;

  for (i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
    len = strlen(types[special[i]].name);
    if (strncmp(l, types[special[i]].name, len) == 0) {
      *end = l + len;
      return special[i];
    }
  }
  return MT_INVALID;
}

uint64_t
mg_signextend(const struct MagicLine *m, uint64_t v)
{
  if (m->flag & MF_UNSIGNED)
    return v;
  switch (m->type) {
    case MT_BEID3:
    case MT_LEID3:
    case MT_INVALID:
      /* file(1) has no case for these and yields its error value */
      return ~(uint64_t)0;
    default:
      break;
  }
  switch (mg_typesize(m->type)) {
    case 1:
      return (uint64_t)(int64_t)(int8_t)v;
    case 2:
      if (m->type == MT_SHORT || m->type == MT_BESHORT || m->type == MT_LESHORT)
        return (uint64_t)(int64_t)(int16_t)v;
      return (uint64_t)(int64_t)(int32_t)v;
    case 4:
      return (uint64_t)(int64_t)(int32_t)v;
    default:
      return v;
  }
}

static int
parse_strmod(struct MagicLine *m, const char **lp)
{
  const char *l = *lp;
  char       *t;

  while (!isspace((unsigned char)*++l)) {
    switch (*l) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        m->range = (uint32_t)strtoul(l, &t, 0);
        l        = t - 1;
        break;
      case 'W':
        m->sflags |= SF_COMPACT_WS;
        break;
      case 'w':
        m->sflags |= SF_COMPACT_OPT_WS;
        break;
      case 'c':
        m->sflags |= SF_IGNORE_LOWER;
        break;
      case 'C':
        m->sflags |= SF_IGNORE_UPPER;
        break;
      case 's':
        m->sflags |= SF_REGEX_START;
        break;
      case 'b':
        m->sflags |= SF_BINTEST;
        break;
      case 't':
        m->sflags |= SF_TEXTTEST;
        break;
      case 'T':
        m->sflags |= SF_TRIM;
        break;
      case 'f':
        m->sflags |= SF_FULL_WORD;
        break;
      case 'B':
      case 'H':
      case 'h':
      case 'L':
      case 'l':
        if (m->type != MT_PSTRING && !(*l == 'l' && m->type == MT_REGEX))
          return -1;
        m->sflags &= ~SF_PSTRING_LEN;
        m->sflags |= *l == 'B'   ? SF_PSTRING_1
                     : *l == 'H' ? SF_PSTRING_2_BE
                     : *l == 'h' ? SF_PSTRING_2_LE
                     : *l == 'L' ? SF_PSTRING_4_BE
                                 : SF_PSTRING_4_LE;
        break;
      case 'J':
        if (m->type != MT_PSTRING)
          return -1;
        m->sflags |= SF_PSTRING_SELF;
        break;
      default:
        return -1;
    }
    /* several slashes are allowed for readability */
    if (l[1] == '/' && !isspace((unsigned char)l[2]))
      l++;
  }
  *lp = l;
  if ((m->type != MT_REGEX || !(m->sflags & SF_REGEX_LINES)) && m->type != MT_PSTRING
      && (m->sflags & SF_PSTRING_LEN))
    return -1;
  switch (m->type) {
    case MT_BESTRING16:
    case MT_LESTRING16:
      return m->sflags ? -1 : 0;
    case MT_STRING:
    case MT_PSTRING:
      return m->sflags & SF_REGEX_START ? -1 : 0;
    case MT_SEARCH:
      return m->range ? 0 : -1;
    case MT_REGEX:
      return m->sflags & (SF_COMPACT_WS | SF_COMPACT_OPT_WS) ? -1 : 0;
    default:
      return -1;
  }
}

static int
hexval(int c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* a string value with c escapes, ending at unescaped white space */
static const char *
getstr(struct MagicLine *m, const char *s)
{
  char  *p = m->value.s, *pmax = m->value.s + MG_STRLEN - 1;
  size_t l;
  int    c, v, h;

  while ((c = (unsigned char)*s++) != '\0') {
    if (isspace(c))
      break;
    if (p >= pmax)
      return NULL;
    if (c != '\\') {
      *p++ = (char)c;
      continue;
    }
    switch (c = (unsigned char)*s++) {
      case '\0':
        s--;
        goto out;
      case 'a':
        *p++ = '\a';
        break;
      case 'b':
        *p++ = '\b';
        break;
      case 'f':
        *p++ = '\f';
        break;
      case 'n':
        *p++ = '\n';
        break;
      case 'r':
        *p++ = '\r';
        break;
      case 't':
        *p++ = '\t';
        break;
      case 'v':
        *p++ = '\v';
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        v = c - '0';
        c = (unsigned char)*s++;
        if (c >= '0' && c <= '7') {
          v = (v << 3) | (c - '0');
          c = (unsigned char)*s++;
          if (c >= '0' && c <= '7')
            v = (v << 3) | (c - '0');
          else
            s--;
        } else {
          s--;
        }
        *p++ = (char)v;
        break;
      case 'x':
        v = 'x';
        if ((h = hexval((unsigned char)*s++)) >= 0) {
          v = h;
          if ((h = hexval((unsigned char)*s++)) >= 0)
            v = (v << 4) + h;
          else
            s--;
        } else {
          s--;
        }
        *p++ = (char)v;
        break;
      default:
        *p++ = (char)c;
        break;
    }
  }
  s--;
out:
  *p        = '\0';
  m->vallen = (uint8_t)(p - m->value.s);
  if (m->type == MT_PSTRING) {
    if (!(l = mg_pstrlen_size(m->sflags)) || m->vallen + l > MG_STRLEN)
      return NULL;
    m->vallen += (uint8_t)l;
  }
  return s;
}

/* the guid is kept with its first three fields little endian, as file(1) keeps it */
static int
getguid(struct MagicLine *m, const char *s)
{
  static const int pos[16] = {6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34};
  int              i, h1, h2;

  if (strlen(s) < 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
    return -1;
  for (i = 0; i < 16; i++) {
    if ((h1 = hexval((unsigned char)s[pos[i]])) < 0
        || (h2 = hexval((unsigned char)s[pos[i] + 1])) < 0)
      return -1;
    m->value.guid[i] = (unsigned char)(h1 << 4 | h2);
  }
  return 0;
}

static int
getvalue(struct MagicLine *m, const char **p)
{
  regex_t     rx;
  uint64_t    ull, x;
  size_t      ts;
  const char *q;
  char       *ep;
  int         over;

  if (mg_isstring(m->type) || m->type == MT_DER) {
    if (!(*p = getstr(m, *p)))
      return -1;
    if (m->type == MT_REGEX) {
      if (regcomp(&rx, m->value.s, REG_EXTENDED | (m->sflags & SF_IGNORE_CASE ? REG_ICASE : 0)))
        return -1;
      regfree(&rx);
    }
    return 0;
  }
  if (m->reln == 'x')
    return 0;
  switch (m->type) {
    case MT_FLOAT:
    case MT_BEFLOAT:
    case MT_LEFLOAT:
      errno      = 0;
      m->value.f = strtof(*p, &ep);
      if (!errno)
        *p = ep;
      return 0;
    case MT_DOUBLE:
    case MT_BEDOUBLE:
    case MT_LEDOUBLE:
      errno      = 0;
      m->value.d = strtod(*p, &ep);
      if (!errno)
        *p = ep;
      return 0;
    case MT_GUID:
    case MT_LEGUID:
    case MT_BEGUID:
      if (getguid(m, *p) < 0)
        return -1;
      *p += 36;
      return 0;
    default:
      break;
  }
  errno      = 0;
  ull        = (uint64_t)strtoull(*p, &ep, 0);
  m->value.q = mg_signextend(m, ull);
  if (*p == ep || !(ts = mg_typesize(m->type)))
    return -1;
  for (q = *p; isspace((unsigned char)*q); q++)
    ;
  if (*q == '-' && ull != UINT64_MAX)
    ull = (uint64_t)(-(int64_t)ull);
  over = 0;
  if (ts < 8) {
    x    = ull & ~(((uint64_t)1 << (ts * 8)) - 1);
    over = x && x != ~(((uint64_t)1 << (ts * 8)) - 1);
  }
  if (over)
    return -1;
  if (!errno) {
    *p = ep;
    eatsize(p);
  }
  return 0;
}

/* a description may hold one printf conversion, and it must suit the type */
static int
check_format(const struct MagicLine *m)
{
  const char *p;
  size_t      len, cnt;
  int         fmt, pass;

  if (!(p = strchr(m->desc, '%')))
    return 0;
  if ((fmt = types[m->type].fmt) == FMT_NONE)
    return -1;
  p++;
  if (!*p)
    return -1;
  switch (fmt) {
    case FMT_NUM:
    case FMT_QUAD:
      while (*p && strchr("+-.#", *p))
        p++;
      for (pass = 0; pass < 2; pass++) {
        for (len = cnt = 0; isdigit((unsigned char)*p); p++, cnt++)
          len = len * 10 + (size_t)(*p - '0');
        if (cnt > 5 || len > 1024)
          return -1;
        if (!pass && *p == '.')
          p++;
      }
      if (fmt == FMT_QUAD && (*p++ != 'l' || *p++ != 'l'))
        return -1;
      switch (*p++) {
        case 'c':
          if (m->type != MT_BYTE)
            return -1;
          break;
        case 'i':
        case 'd':
        case 'u':
        case 'o':
        case 'x':
        case 'X':
          break;
        default:
          return -1;
      }
      break;
    case FMT_FLOAT:
      if (*p == '-')
        p++;
      if (*p == '.')
        p++;
      for (pass = 0; pass < 2; pass++) {
        for (len = cnt = 0; isdigit((unsigned char)*p); p++, cnt++)
          len = len * 10 + (size_t)(*p - '0');
        if (cnt > 5 || len > 1024)
          return -1;
        if (!pass && *p == '.')
          p++;
      }
      if (!*p || !strchr("eEfFgG", *p++))
        return -1;
      break;
    default:
      if (*p == '-')
        p++;
      while (isdigit((unsigned char)*p))
        p++;
      if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p))
          p++;
      }
      if (*p++ != 's')
        return -1;
      break;
  }
  return strchr(p, '%') ? -1 : 0;
}

static struct MagicLine *
group_add(struct Group *g)
{
  struct MagicLine *v;
  size_t            cap;

  if (g->n == g->cap) {
    cap = g->cap ? g->cap * 2 : 8;
    if (!(v = realloc(g->v, cap * sizeof(*v))))
      return NULL;
    g->v   = v;
    g->cap = cap;
  }
  memset(&g->v[g->n], 0, sizeof(g->v[0]));
  return &g->v[g->n++];
}

static void
group_finish(struct TextLoad *t)
{
  struct GroupSet *s;
  struct Group    *g;
  size_t           cap;

  if (!t->cur.n) {
    free(t->cur.v);
    memset(&t->cur, 0, sizeof(t->cur));
    return;
  }
  s = &t->set[t->cur.v[0].type == MT_NAME ? 1 : 0];
  if (s->n == s->cap) {
    cap = s->cap ? s->cap * 2 : 256;
    if (!(g = realloc(s->g, cap * sizeof(*g)))) {
      t->nomem = 1;
      free(t->cur.v);
      memset(&t->cur, 0, sizeof(t->cur));
      return;
    }
    s->g   = g;
    s->cap = cap;
  }
  s->g[s->n++] = t->cur;
  memset(&t->cur, 0, sizeof(t->cur));
}

static int
parse_offset(struct MagicLine *m, const char **lp)
{
  const char *l = *lp;
  char       *e;
  int         op;

  if (*l == '&') {
    l++;
    m->flag |= MF_OFFADD;
  }
  if (*l == '(') {
    l++;
    m->flag |= MF_INDIR;
    if (m->flag & MF_OFFADD)
      m->flag = (uint16_t)((m->flag & ~MF_OFFADD) | MF_INDIROFFADD);
    if (*l == '&') {
      l++;
      m->flag |= MF_OFFADD;
    }
  }
  if (!m->cont && (m->flag & (MF_OFFADD | MF_INDIROFFADD)))
    return -1;
  if (*l == '-' || *l == '+') {
    m->flag |= *l == '-' ? MF_OFFNEG : MF_OFFPOS;
    l++;
  }
  m->offset = (int32_t)strtol(l, &e, 0);
  if (e == l)
    return -1;
  l = e;
  if (m->flag & MF_INDIR) {
    m->in_type = MT_LONG;
    if (*l == '.' || *l == ',') {
      if (*l++ == ',')
        m->in_op |= OP_SIGNED;
      switch (*l++) {
        case 'l':
          m->in_type = MT_LELONG;
          break;
        case 'L':
          m->in_type = MT_BELONG;
          break;
        case 'm':
          m->in_type = MT_MELONG;
          break;
        case 'h':
        case 's':
          m->in_type = MT_LESHORT;
          break;
        case 'H':
        case 'S':
          m->in_type = MT_BESHORT;
          break;
        case 'c':
        case 'b':
        case 'C':
        case 'B':
          m->in_type = MT_BYTE;
          break;
        case 'e':
        case 'f':
        case 'g':
          m->in_type = MT_LEDOUBLE;
          break;
        case 'E':
        case 'F':
        case 'G':
          m->in_type = MT_BEDOUBLE;
          break;
        case 'i':
          m->in_type = MT_LEID3;
          break;
        case 'I':
          m->in_type = MT_BEID3;
          break;
        case 'o':
          m->in_type = MT_OCTAL;
          break;
        case 'q':
          m->in_type = MT_LEQUAD;
          break;
        case 'Q':
          m->in_type = MT_BEQUAD;
          break;
        default:
          return -1;
      }
    }
    if (*l == '~') {
      m->in_op |= OP_INVERSE;
      l++;
    }
    if ((op = get_op((unsigned char)*l)) != -1) {
      m->in_op |= (uint8_t)op;
      l++;
    }
    if (*l == '(') {
      m->in_op |= OP_INDIRECT;
      l++;
    }
    if (isdigit((unsigned char)*l) || *l == '-') {
      m->in_offset = (int32_t)strtol(l, &e, 0);
      if (e == l)
        return -1;
      l = e;
    }
    if (*l++ != ')' || ((m->in_op & OP_INDIRECT) && *l++ != ')'))
      return -1;
  }
  *lp = l;
  return 0;
}

static int
parse_type(struct MagicLine *m, const char **lp)
{
  const char *l = *lp;
  int         t = MT_INVALID;

  if (*l == 'u') {
    if ((t = type_keyword(l + 1, &l)) == MT_INVALID)
      t = type_posix(l, &l);
    if (t != MT_INVALID)
      m->flag |= MF_UNSIGNED;
  } else {
    t = type_keyword(l, &l);
    if (t == MT_INVALID) {
      if (*l == 'd') {
        t = type_posix(l, &l);
      } else if (*l == 's' && !isalpha((unsigned char)l[1])) {
        t = MT_STRING;
        l++;
      }
    }
  }
  if (t == MT_INVALID)
    t = type_special(l, &l);
  if (t == MT_INVALID || (t == MT_NAME && m->cont))
    return -1;
  m->type = (uint8_t)t;
  *lp     = l;
  return 0;
}

static int
parse_mask(struct MagicLine *m, const char **lp)
{
  const char *l = *lp;
  char       *e;
  int         op;

  if (*l == '~') {
    if (!mg_isstring(m->type))
      m->mask_op |= OP_INVERSE;
    l++;
  }
  m->sflags = m->type == MT_PSTRING ? SF_PSTRING_1 : 0;
  if ((op = get_op((unsigned char)*l)) != -1) {
    if (mg_isstring(m->type)) {
      if (op != OP_DIVIDE)
        return -1;
      if (m->type == MT_INDIRECT) {
        while (!isspace((unsigned char)*++l)) {
          if (*l != 'r')
            return -1;
          m->sflags |= SF_INDIRECT_REL;
        }
      } else if (parse_strmod(m, &l) < 0) {
        return -1;
      }
    } else {
      l++;
      m->mask_op |= (uint8_t)op;
      m->mask = mg_signextend(m, (uint64_t)strtoull(l, &e, 0));
      l       = e;
      eatsize(&l);
    }
  }
  *lp = l;
  return 0;
}

static int
parse_line(struct TextLoad *t, const char *line, const char *fname, uint32_t lineno)
{
  struct MagicLine *m;
  const char       *l = line;
  size_t            i;
  unsigned          cont = 0;

  while (*l == '>') {
    l++;
    cont++;
  }
  if (cont >= MG_LEVELS)
    return -1;
  if (!cont) {
    group_finish(t);
    t->skip = 0;
  } else if (t->skip || !t->cur.n) {
    return -1;
  }
  if (!(m = group_add(&t->cur))) {
    t->nomem = 1;
    return -1;
  }
  m->cont   = (uint8_t)cont;
  m->lineno = lineno;
  if (parse_offset(m, &l) < 0)
    goto bad;
  l = eatab(l);
  if (parse_type(m, &l) < 0 || parse_mask(m, &l) < 0)
    goto bad;
  l = eatab(l);
  switch (*l) {
    case '>':
    case '<':
      m->reln = (uint8_t)*l++;
      if (*l == '=')
        goto bad;
      break;
    case '&':
    case '^':
    case '=':
      m->reln = (uint8_t)*l++;
      /* hp compatibility: &= and such mean & */
      if (*l == '=')
        l++;
      break;
    case '!':
      m->reln = (uint8_t)*l++;
      break;
    default:
      m->reln = '=';
      if (*l == 'x' && (((unsigned char)l[1] < 0x80 && isspace((unsigned char)l[1])) || !l[1])) {
        m->reln = 'x';
        l++;
      }
      break;
  }
  if (m->reln != 'x') {
    if (!*l || getvalue(m, &l) < 0)
      goto bad;
  }
  l = eatab(l);
  if (l[0] == '\b') {
    l++;
    m->flag |= MF_NOSPACE;
  } else if (l[0] == '\\' && l[1] == 'b') {
    l += 2;
    m->flag |= MF_NOSPACE;
  }
  for (i = 0; i < MG_DESCLEN && (m->desc[i++] = *l++) != '\0';)
    ;
  m->desc[MG_DESCLEN - 1] = '\0';
  /* file(1) tucks the source name behind an empty description, and sorts on it */
  if (!m->desc[0]) {
    for (i = 0; i < MG_DESCLEN - 2 && fname[i]; i++)
      m->desc[i + 1] = fname[i];
  }
  if (check_format(m) < 0)
    goto bad;
  return 0;
bad:
  t->cur.n--;
  if (!cont)
    t->skip = 1;
  return -1;
}

static int
goodchar(int c, const char *extra)
{
  return (c < 0x80 && isalnum(c)) || (c && strchr(extra, c));
}

static int
parse_extra(const char *l, char *buf, size_t len, const char *extra, int nt)
{
  size_t i;

  if (buf[0])
    return -1;
  l = eatab(l);
  for (i = 0; *l && i < len && goodchar((unsigned char)*l, extra); i++)
    buf[i] = *l++;
  if (i == len && *l) {
    if (nt)
      buf[len - 1] = '\0';
  } else if (nt) {
    buf[i] = '\0';
  }
  return i ? 0 : -1;
}

static int
parse_strength(struct MagicLine *m, const char *l)
{
  unsigned long factor;
  char         *e;

  if (m->factor_op || m->type == MT_NAME)
    return -1;
  l = eatab(l);
  switch (*l) {
    case '\0':
      break;
    case '+':
    case '-':
    case '*':
    case '/':
      m->factor_op = (uint8_t)*l++;
      break;
    default:
      return -1;
  }
  l      = eatab(l);
  factor = strtoul(l, &e, 0);
  if (factor > 255 || (*e && !isspace((unsigned char)*e)) || (factor == 0 && m->factor_op == '/')) {
    m->factor_op = 0;
    m->factor    = 0;
    return -1;
  }
  m->factor = (uint8_t)factor;
  return 0;
}

static int
parse_bang(struct TextLoad *t, const char *line)
{
  struct MagicLine *m;
  struct Group     *g = &t->cur;
  size_t            last;

  if (t->skip || !g->n)
    return -1;
  last = g->n - 1;
  m    = &g->v[last];
  if (strncmp(line, "mime", 4) == 0 && line[4]) {
    if (!m->desc[0])
      return -1;
    return parse_extra(line + 4, m->mime, MG_MIMELEN, "+-/.$?:{};=", 1);
  }
  if (strncmp(line, "apple", 5) == 0 && line[5]) {
    /* only the first line takes part in sorting, so only it keeps these */
    if (!m->desc[0] || last)
      return last ? 0 : -1;
    return parse_extra(line + 5, g->apple, sizeof(g->apple), "!+-./?", 0);
  }
  if (strncmp(line, "ext", 3) == 0 && line[3]) {
    if (!m->desc[0] || last)
      return last ? 0 : -1;
    return parse_extra(line + 3, g->ext, sizeof(g->ext), ",!+-/@?_$&~.", 0);
  }
  if (strncmp(line, "strength", 8) == 0 && line[8])
    return parse_strength(&g->v[0], line + 8);
  return -1;
}

static int
text_file(struct TextLoad *t, const char *path)
{
  FILE    *fp;
  char    *line = NULL;
  size_t   cap  = 0;
  ssize_t  len;
  uint32_t lineno = 0;

  if (!(fp = fopen(path, "r")))
    return -1;
  while ((len = getline(&line, &cap, fp)) != -1) {
    if (!len)
      continue;
    if (line[len - 1] == '\n') {
      lineno++;
      line[len - 1] = '\0';
    }
    switch (line[0]) {
      case '\0':
      case '#':
        continue;
      case '!':
        if (line[1] == ':') {
          parse_bang(t, line + 2);
          continue;
        }
        /* fallthrough */
      default:
        if (parse_line(t, line, path, lineno) < 0)
          t->nbad++;
        break;
    }
    if (t->nomem)
      break;
  }
  free(line);
  fclose(fp);
  group_finish(t);
  return t->nomem ? -1 : 0;
}

static size_t
nonmagic(const char *s)
{
  size_t n = 0;

  for (; *s; s++) {
    switch (*s) {
      case '\\':
        if (!*++s)
          s--;
        n++;
        break;
      case '?':
      case '*':
      case '.':
      case '+':
      case '^':
      case '$':
        break;
      case '[':
        while (*s && *s != ']')
          s++;
        s--;
        break;
      case '{':
        while (*s && *s != '}')
          s++;
        if (!*s)
          s--;
        break;
      default:
        n++;
        break;
    }
  }
  return n ? n : 1;
}

static long
strength(const struct MagicLine *m)
{
  long   v = 2 * MULT;
  size_t n;

  switch (m->type) {
    case MT_DEFAULT:
      v = 0;
      goto factor;
    case MT_PSTRING:
    case MT_STRING:
    case MT_OCTAL:
      v += m->vallen * MULT;
      break;
    case MT_BESTRING16:
    case MT_LESTRING16:
      v += m->vallen * MULT / 2;
      break;
    case MT_SEARCH:
      if (m->vallen)
        v += m->vallen * (MULT / m->vallen > 1 ? MULT / m->vallen : 1);
      break;
    case MT_REGEX:
      n = nonmagic(m->value.s);
      v += (long)(n * (MULT / n > 1 ? MULT / n : 1));
      break;
    case MT_INDIRECT:
    case MT_NAME:
    case MT_USE:
    case MT_CLEAR:
      break;
    case MT_DER:
      v += MULT;
      break;
    default:
      v += (long)mg_typesize(m->type) * MULT;
      break;
  }
  switch (m->reln) {
    case 'x':
    case '!':
      v = 0;
      break;
    case '=':
      v += MULT;
      break;
    case '>':
    case '<':
      v -= 2 * MULT;
      break;
    case '^':
    case '&':
      v -= MULT;
      break;
    default:
      break;
  }
factor:
  switch (m->factor_op) {
    case '+':
      v += m->factor;
      break;
    case '-':
      v -= m->factor;
      break;
    case '*':
      v *= m->factor;
      break;
    case '/':
      v /= m->factor;
      break;
    default:
      break;
  }
  if (v <= 0)
    v = 1;
  if (!m->desc[0])
    v++;
  return v;
}

/* the first line as file(1) lays it out in memory, for its tie break between equal strengths */
static void
group_image(struct Group *g)
{
  const struct MagicLine *m = &g->v[0];
  unsigned char          *p = g->img;

  memset(p, 0, MG_NEWSIZE);
  memcpy(p, &m->flag, 2);
  p[2]  = m->cont;
  p[3]  = m->factor;
  p[4]  = m->reln;
  p[5]  = m->vallen;
  p[6]  = m->type;
  p[7]  = m->in_type;
  p[8]  = m->in_op;
  p[9]  = m->mask_op;
  p[11] = m->factor_op;
  memcpy(p + 12, &m->offset, 4);
  memcpy(p + 16, &m->in_offset, 4);
  if (mg_isstring(m->type)) {
    memcpy(p + 24, &m->range, 4);
    memcpy(p + 28, &m->sflags, 4);
  } else {
    memcpy(p + 24, &m->mask, 8);
  }
  memcpy(p + 32, &m->value, MG_STRLEN);
  memcpy(p + 160, m->desc, MG_DESCLEN);
  memcpy(p + 224, m->mime, MG_MIMELEN);
  memcpy(p + 304, g->apple, sizeof(g->apple));
  memcpy(p + 312, g->ext, sizeof(g->ext));
}

static int
group_cmp(const void *a, const void *b)
{
  const struct Group *ga = a, *gb = b;
  int                 x;

  if (ga->strength != gb->strength)
    return ga->strength > gb->strength ? -1 : 1;
  if (!(x = memcmp(ga->img, gb->img, MG_NEWSIZE)))
    return 0;
  return x > 0 ? -1 : 1;
}

/* binary or text test: file(1) means to look at every line but only reaches the first */
static void
group_testtype(struct Group *g)
{
  struct MagicLine *m = &g->v[0];

  switch (m->type) {
    case MT_STRING:
    case MT_PSTRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
    case MT_REGEX:
    case MT_SEARCH:
      if (m->sflags & SF_BINTEST)
        m->flag |= MF_BINTEST;
      if (m->sflags & SF_TEXTTEST)
        m->flag |= MF_TEXTTEST;
      if (m->flag & (MF_TEXTTEST | MF_BINTEST))
        break;
      if ((m->type != MT_REGEX && m->type != MT_SEARCH)
          || mg_looks_utf8(m->value.us, m->vallen, NULL, NULL) <= 0)
        m->flag |= MF_BINTEST;
      else
        m->flag |= MF_TEXTTEST;
      break;
    case MT_DEFAULT:
    case MT_INVALID:
    case MT_NAME:
    case MT_USE:
    case MT_CLEAR:
    case MT_INDIRECT:
      break;
    default:
      m->flag |= MF_BINTEST;
      break;
  }
}

static int
text_flatten(struct TextLoad *t, struct MagicDb *db)
{
  struct GroupSet *s;
  size_t           i, j, n;

  for (i = 0; i < MG_SETS; i++) {
    s = &t->set[i];
    for (j = n = 0; j < s->n; j++) {
      group_testtype(&s->g[j]);
      s->g[j].strength = strength(&s->g[j].v[0]);
      group_image(&s->g[j]);
      n += s->g[j].n;
    }
    if (!n)
      continue;
    qsort(s->g, s->n, sizeof(s->g[0]), group_cmp);
    if (!(db->v[i] = malloc(n * sizeof(*db->v[i]))))
      return -1;
    for (j = 0; j < s->n; j++) {
      memcpy(db->v[i] + db->n[i], s->g[j].v, s->g[j].n * sizeof(*db->v[i]));
      db->n[i] += s->g[j].n;
    }
  }
  return 0;
}

static int
strpcmp(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static struct MagicDb *
text_load(const char *path, int isdir)
{
  struct TextLoad t;
  struct MagicDb *db = NULL;
  struct dirent  *d;
  struct stat     st;
  DIR            *dir;
  char          **names = NULL, **nn, *p;
  size_t          n = 0, cap = 0, i, j;
  int             e = 0, loaded = 0;

  memset(&t, 0, sizeof(t));
  if (isdir) {
    if (!(dir = opendir(path)))
      return NULL;
    while ((d = readdir(dir))) {
      if (d->d_name[0] == '.')
        continue;
      if (!(p = malloc(strlen(path) + strlen(d->d_name) + 2))) {
        e = ENOMEM;
        break;
      }
      sprintf(p, "%s/%s", path, d->d_name);
      if (stat(p, &st) < 0 || !S_ISREG(st.st_mode)) {
        free(p);
        continue;
      }
      if (n == cap) {
        cap = cap ? cap * 2 : 64;
        if (!(nn = realloc(names, cap * sizeof(*names)))) {
          free(p);
          e = ENOMEM;
          break;
        }
        names = nn;
      }
      names[n++] = p;
    }
    closedir(dir);
    if (!e) {
      qsort(names, n, sizeof(*names), strpcmp);
      for (i = 0; i < n && !t.nomem; i++) {
        if (text_file(&t, names[i]) == 0)
          loaded = 1;
      }
    }
    for (i = 0; i < n; i++)
      free(names[i]);
    free(names);
  } else if (text_file(&t, path) == 0) {
    loaded = 1;
  } else if (!t.nomem) {
    e = errno;
  }
  if (t.nomem)
    e = ENOMEM;
  if (!e && !loaded)
    e = ENOENT;
  /* a file that is all errors is not magic, a file of comments is an empty database */
  if (!e && t.nbad && !t.set[0].n && !t.set[1].n)
    e = ENOEXEC;
  if (!e) {
    if (!(db = calloc(1, sizeof(*db))) || text_flatten(&t, db) < 0) {
      mg_free(db);
      db = NULL;
      e  = ENOMEM;
    }
  }
  for (i = 0; i < MG_SETS; i++) {
    for (j = 0; j < t.set[i].n; j++)
      free(t.set[i].g[j].v);
    free(t.set[i].g);
  }
  free(t.cur.v);
  if (!db)
    errno = e;
  return db;
}

/* one list entry: x.mgc if it loads, else the text source x */
static struct MagicDb *
load_one(const char *path, struct stat *id)
{
  struct MagicDb *db;
  struct stat     st;
  char           *mgc;
  size_t          len = strlen(path);
  int             e;

  if (len >= 4 && strcmp(path + len - 4, ".mgc") == 0) {
    if (stat(path, &st) < 0)
      return NULL;
    *id = st;
    return mgc_load(path);
  }
  if (!(mgc = malloc(len + 5))) {
    errno = ENOMEM;
    return NULL;
  }
  memcpy(mgc, path, len);
  strcpy(mgc + len, ".mgc");
  e = ENOENT;
  if (stat(mgc, &st) == 0 && S_ISREG(st.st_mode)) {
    *id = st;
    db  = mgc_load(mgc);
    e   = errno;
    if (db || stat(path, &st) < 0) {
      free(mgc);
      errno = e;
      return db;
    }
  }
  free(mgc);
  if (stat(path, &st) < 0) {
    errno = e;
    return NULL;
  }
  *id = st;
  return text_load(path, S_ISDIR(st.st_mode));
}

int
mg_load(struct Magic *mg, const char *list)
{
  struct MagicDb **tail = &mg->db, *db;
  struct stat      st, *seen = NULL, *ns;
  const char      *p, *e;
  char            *path;
  size_t           nseen = 0, i, len;
  int              err   = ENOENT;

  for (p = list; *p; p = *e ? e + 1 : e) {
    if (!(e = strchr(p, ':')))
      e = p + strlen(p);
    if ((len = (size_t)(e - p)) == 0)
      continue;
    if (!(path = malloc(len + 1))) {
      err = ENOMEM;
      break;
    }
    memcpy(path, p, len);
    path[len] = '\0';
    memset(&st, 0, sizeof(st));
    db = load_one(path, &st);
    free(path);
    if (!db) {
      if (errno != ENOENT && err == ENOENT)
        err = errno;
      continue;
    }
    /* the default list names the same file twice where one path links to another */
    for (i = 0; i < nseen; i++) {
      if (seen[i].st_dev == st.st_dev && seen[i].st_ino == st.st_ino)
        break;
    }
    if (i < nseen) {
      mg_free(db);
      continue;
    }
    if (!(ns = realloc(seen, (nseen + 1) * sizeof(*seen)))) {
      mg_free(db);
      err = ENOMEM;
      break;
    }
    seen          = ns;
    seen[nseen++] = st;
    *tail         = db;
    tail          = &db->next;
  }
  free(seen);
  if (!mg->db) {
    errno = err;
    return -1;
  }
  return 0;
}
