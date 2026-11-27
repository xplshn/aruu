/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBMAGIC_MGINT_H
#define ARUU_LIBMAGIC_MGINT_H

/* internal to libmagic: shared by its source files, never by callers */

#include "magic.h"

#include <locale.h>
#include <regex.h>
#include <stddef.h>
#include <stdint.h>

/* sizes of the text fields of one test line, as file(1) stores them */
#define MG_STRLEN  128
#define MG_DESCLEN 64
#define MG_MIMELEN 80

/* the entry size and magic number of a compiled magic.mgc */
#define MG_MAGICNO 0xF11E041Cu
#define MG_OLDSIZE 376
#define MG_NEWSIZE 432
#define MG_LEVELS  256
#define MG_SETS    2

/* search limits file(1) applies by default */
#define MG_INDIR_MAX    50
#define MG_NAME_MAX     150
#define MG_REGEX_MAX    8192
#define MG_ENCODING_MAX MAGIC_CHARSET_MAX

/* test types, numbered as file 5.48 (format version 21) numbers them */
enum MagicType {
  MT_INVALID,
  MT_BYTE,
  MT_SHORT,
  MT_DEFAULT,
  MT_LONG,
  MT_STRING,
  MT_DATE,
  MT_BESHORT,
  MT_BELONG,
  MT_BEDATE,
  MT_LESHORT,
  MT_LELONG,
  MT_LEDATE,
  MT_PSTRING,
  MT_LDATE,
  MT_BELDATE,
  MT_LELDATE,
  MT_REGEX,
  MT_BESTRING16,
  MT_LESTRING16,
  MT_SEARCH,
  MT_MEDATE,
  MT_MELDATE,
  MT_MELONG,
  MT_QUAD,
  MT_LEQUAD,
  MT_BEQUAD,
  MT_QDATE,
  MT_LEQDATE,
  MT_BEQDATE,
  MT_QLDATE,
  MT_LEQLDATE,
  MT_BEQLDATE,
  MT_FLOAT,
  MT_BEFLOAT,
  MT_LEFLOAT,
  MT_DOUBLE,
  MT_BEDOUBLE,
  MT_LEDOUBLE,
  MT_BEID3,
  MT_LEID3,
  MT_INDIRECT,
  MT_QWDATE,
  MT_LEQWDATE,
  MT_BEQWDATE,
  MT_NAME,
  MT_USE,
  MT_CLEAR,
  MT_DER,
  MT_GUID,
  MT_LEGUID,
  MT_BEGUID,
  MT_OFFSET,
  MT_BEVARINT,
  MT_LEVARINT,
  MT_MSDOSDATE,
  MT_LEMSDOSDATE,
  MT_BEMSDOSDATE,
  MT_MSDOSTIME,
  MT_LEMSDOSTIME,
  MT_BEMSDOSTIME,
  MT_OCTAL,
  MT_NTYPES
};

/* line flags, bit for bit as stored in magic.mgc */
enum MagicFlag {
  MF_INDIR       = 0x01,
  MF_OFFADD      = 0x02,
  MF_INDIROFFADD = 0x04,
  MF_UNSIGNED    = 0x08,
  MF_NOSPACE     = 0x10,
  MF_BINTEST     = 0x20,
  MF_TEXTTEST    = 0x40,
  MF_OFFNEG      = 0x80,
  MF_OFFPOS      = 0x100
};

/* string modifiers, bit for bit as stored in magic.mgc */
enum MagicStrFlag {
  SF_COMPACT_WS     = 1 << 0,
  SF_COMPACT_OPT_WS = 1 << 1,
  SF_IGNORE_LOWER   = 1 << 2,
  SF_IGNORE_UPPER   = 1 << 3,
  SF_REGEX_START    = 1 << 4,
  SF_TEXTTEST       = 1 << 5,
  SF_BINTEST        = 1 << 6,
  SF_PSTRING_1      = 1 << 7,
  SF_PSTRING_2_BE   = 1 << 8,
  SF_PSTRING_2_LE   = 1 << 9,
  SF_PSTRING_4_BE   = 1 << 10,
  SF_PSTRING_4_LE   = 1 << 11,
  SF_PSTRING_SELF   = 1 << 12,
  SF_TRIM           = 1 << 13,
  SF_FULL_WORD      = 1 << 14
};

/* regex /l shares the bit of the 4 byte little endian pascal length */
#define SF_REGEX_LINES SF_PSTRING_4_LE
#define SF_PSTRING_LEN                                                                             \
  (SF_PSTRING_1 | SF_PSTRING_2_BE | SF_PSTRING_2_LE | SF_PSTRING_4_BE | SF_PSTRING_4_LE)
#define SF_IGNORE_CASE  (SF_IGNORE_LOWER | SF_IGNORE_UPPER)
#define SF_INDIRECT_REL (1 << 0)

/* mask and indirection operators */
enum MagicOp { OP_AND, OP_OR, OP_XOR, OP_ADD, OP_MINUS, OP_MULTIPLY, OP_DIVIDE, OP_MODULO };

#define OP_MASK     0x07
#define OP_SIGNED   0x20
#define OP_INVERSE  0x40
#define OP_INDIRECT 0x80

union MagicValue {
  uint8_t       b;
  uint16_t      h;
  uint32_t      l;
  uint64_t      q;
  int8_t        sb;
  int16_t       sh;
  int32_t       sl;
  int64_t       sq;
  float         f;
  double        d;
  unsigned char hs[2];
  unsigned char hl[4];
  unsigned char hq[8];
  char          s[MG_STRLEN];
  unsigned char us[MG_STRLEN];
  unsigned char guid[16];
};

/* one line of magic(5) */
struct MagicLine {
  int32_t          offset;
  int32_t          in_offset;
  uint64_t         mask;   /* numeric types */
  uint32_t         range;  /* string types */
  uint32_t         sflags; /* string types */
  uint32_t         lineno;
  uint16_t         flag;
  uint8_t          cont;
  uint8_t          factor;
  uint8_t          factor_op;
  uint8_t          reln;
  uint8_t          vallen;
  uint8_t          type;
  uint8_t          in_type;
  uint8_t          in_op;
  uint8_t          mask_op;
  uint8_t          rxbad; /* regcomp failed once, dont retry */
  regex_t         *rx;    /* compiled lazily on first use */
  union MagicValue value;
  char             desc[MG_DESCLEN];
  char             mime[MG_MIMELEN];
};

/* one loaded database: set 0 holds the tests, set 1 the named tests */
struct MagicDb {
  struct MagicLine *v[MG_SETS];
  size_t            n[MG_SETS];
  struct MagicDb   *next;
};

/* a top level test and its continuations, and how far into a file they
 * read: -1 when that is known only once the file is read */
struct MagicGroup {
  struct MagicDb *db;
  size_t          first, n;
  long long       reach;
};
/* a mime type a group can give */
struct MagicMime {
  const char *mime;
  size_t      group;
};
/* what magic_prepare works out: the groups, the types by name, and the
 * farthest any group with a known reach reads */
struct MagicIndex {
  struct MagicGroup *group;
  size_t             ngroup;
  struct MagicMime  *mime;
  size_t             nmime;
  long long          reach;
  size_t            *far; /* the groups that read past MAGIC_CHARSET_MAX */
  size_t             nfar;
  size_t             tail; /* the most any test reads back from the end */
};
struct Magic {
  struct MagicDb    *db;
  locale_t           cloc; /* regex and ctype run in the posix locale */
  struct MagicIndex *ix;   /* NULL until magic_prepare */
};

/* growing output string, empty on allocation failure with err set */
struct MagicOut {
  char  *s;
  size_t n;
  size_t cap;
  int    err;
};

/* the bytes a test can see: the head of a file, or a slice of it */
struct MagicBuf {
  const unsigned char *p;
  size_t               n;
  int                  file; /* negative offsets read the tail of the file being classified */
};

/* state of one classification of one file */
struct MagicRun {
  struct Magic        *m;
  struct MagicBuf      head;
  int                  fd;    /* -1 when only the buffer is known */
  long long            fsize; /* -1 when unknown */
  unsigned char       *tail;  /* last bytes of a large file, for negative offsets */
  size_t               tlen;
  int                  tstate; /* 0 not read, 1 read, 2 part of head, -1 unreadable */
  int                  mime;   /* 1 to collect the mime type instead of the description */
  int                  xmode;  /* nonzero picks the first branch of ${x?a:b} */
  struct MagicOut      out;
  int32_t              offset;
  int32_t              eoffset;
  union MagicValue     val;
  const unsigned char *ss; /* search and regex window */
  size_t               slen;
  size_t               soff;
  size_t               rmlen;
  int32_t              lioff[MG_LEVELS];
  int                  ligot[MG_LEVELS];
  unsigned             nindir;
  unsigned             nnames;
  const size_t        *pick; /* when set, only these groups of the index are tried */
  size_t               npick;
};

/* mgindex.c */
void   mg_index_free(struct MagicIndex *);
size_t mg_index_pick(const struct MagicIndex *, const char *, size_t **, long long *);
size_t mg_index_probe(struct MagicRun *, unsigned char *, size_t, size_t, size_t *);
/* mgload.c */
int      mg_load(struct Magic *, const char *);
void     mg_free(struct MagicDb *);
int      mg_isstring(int);
size_t   mg_typesize(int);
uint64_t mg_signextend(const struct MagicLine *, uint64_t);

/* mgmatch.c */
int    mg_softmagic(struct MagicRun *, const struct MagicBuf *, int, int);
int    mg_top_holds(struct MagicRun *, struct MagicLine *, const struct MagicBuf *);
int    mg_find(struct Magic *, const char *, struct MagicLine **, size_t *);
size_t mg_pstrlen_size(uint32_t);

/* mgtext.c */
int mg_looks_utf8(const unsigned char *, size_t, uint32_t *, size_t *);
int mg_encoding(
    const unsigned char *buf,
    size_t               n,
    uint32_t           **ubuf,
    size_t              *ulen,
    const char         **code,
    const char         **code_mime,
    const char         **type
);
int mg_ascmagic(struct MagicRun *, int);
int mg_istar(const unsigned char *, size_t);
int mg_isjson(const unsigned char *, size_t);
int mg_iscsv(const unsigned char *, size_t);

/* mgelf.c */
int mg_elf(struct MagicRun *, struct MagicOut *);

/* magic.c */
int       mg_add(struct MagicOut *, const char *, size_t);
int       mg_puts(struct MagicOut *, const char *);
int       mg_printf(struct MagicOut *, const char *, ...);
void      mg_printable(char *, size_t, const char *, size_t);
int       mg_varexpand(char *, size_t, const char *, int);
int       mg_replace_end(struct MagicOut *, const char *, const char *);
long long mg_pread(struct MagicRun *, void *, size_t, long long);

#endif
