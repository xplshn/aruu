#include "../config.h"

#include "parse_asm.h"
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h> /* strtoul */
#include <string.h>
#include <strings.h>

#if __STDC_NO_VLA__
#include <alloca.h>
#endif

#include "ir_asm.h"
#include "table.h"
#include "tcutil.h"

#include "arch.h"

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
const char kSegText[]   = "__TEXT";
const char kSecText[]   = "__text";
const char kSegRodata[] = "__DATA";
const char kSecRodata[] = "__const";
const char kSegData[]   = "__DATA";
const char kSecData[]   = "__data";
const char kSegBss[]    = "__DATA";
const char kSecBss[]    = "__bss";
#else
const char kSecText[]   = ".text";
const char kSecRodata[] = ".rodata";
const char kSecData[]   = ".data";
const char kSecBss[]    = ".bss";
#endif

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
#define WEAK "weak_definition"
#else
#define WEAK "weak"
#endif

static const char *kDirectiveTable[] = {
    "ascii",
    "string",
    "section",
    "text",
    "data",
    "bss",
    "align",
    "p2align",
    "balign",
    "type",
    "byte",
    "short",
    "long",
    "quad",
    "comm",
    "zero",
    "globl",
    "local",
    WEAK,
    "extern",
    "int",
    "word",
    "2byte",
    "4byte",
    "8byte",
    "fill",
    "space",
    "skip",
    "set",
    "equ",
    "file",
    "loc",
    "ident",
    "previous",
#ifndef __NO_FLONUM
    "float",
    "double",
#endif
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
    "subsections_via_symbols",
#endif
    "size",
};

struct SectionInfo *
get_section_info(struct ParseInfo *info, const char *name, const char *segname, int flag)
{
  const struct Name  *key = alloc_name(name, NULL, 1);
  struct SectionInfo *section;
  if (!table_try_get(info->section_infos, key, (void **)&section)) {
    section                = calloc_or_die(sizeof(*section));
    section->name          = key;
    section->segname       = segname;
    section->irs           = new_vector();
    section->flag          = flag;
    section->index         = 0;
    section->start_address = 0;
    section->ds            = NULL;
    section->align         = 1;
    section->bss_size      = 0;
    section->rela_count    = 0;
    section->rela_buf      = NULL;
    if (!(flag & SF_BSS)) {
      struct DataStorage *ds = calloc_or_die(sizeof(*ds));
      data_init(ds);
      section->ds = ds;
    }
    table_put(info->section_infos, key, section);
  }
  return section;
}

struct SectionInfo *
set_current_section(struct ParseInfo *info, const char *name, const char *segname, int flag)
{
  struct SectionInfo *section = get_section_info(info, name, segname, flag);
  info->current_section       = section;
  return section;
}

static struct LabelInfo *
new_label(struct SectionInfo *section)
{
  struct LabelInfo *info = calloc_or_die(sizeof(*info));
  info->section          = section;
  info->flag             = 0;
  info->address          = 0;
  info->kind             = LK_NONE;
  info->size             = 0;
  info->align            = 0;
  return info;
}

struct LabelInfo *
add_label_table(
    struct Table       *label_table,
    const struct Name  *label,
    struct SectionInfo *section,
    int                 define,
    int                 global
)
{
  struct LabelInfo *info = table_get(label_table, label);
  if (info != NULL) {
    if (define) {
      if ((info->flag & LF_DEFINED) != 0) {
        fprintf(stderr, "`%.*s' already defined\n", NAMES(label));
        return NULL;
      }
      info->address = 1;
      info->section = section;
    }
  } else {
    info = new_label(section);
    table_put(label_table, label, info);
  }
  if (define)
    info->flag |= LF_DEFINED;
  if (global)
    info->flag |= LF_GLOBAL;
  return info;
}

int
parse_error(struct ParseInfo *info, const char *message)
{
  fprintf(stderr, "%s(%d): %s\n", info->filename, info->lineno, message);
  fprintf(stderr, "%s\n", info->rawline);
  ++info->error_count;
  return 0;
}

static enum DirectiveType
find_directive(const char *p, size_t n)
{
  const char **table = kDirectiveTable;
  size_t       i;

  for (i = 0; i < ARRAY_SIZE(kDirectiveTable); ++i) {
    const char *name = table[i];
    if (strncasecmp(p, name, n) == 0 && name[n] == '\0') {
      return i + 1;
    }
  }
  /* cfi directives are a family, tolerate them all */
  if (n >= 4 && strncasecmp(p, "cfi_", 4) == 0)
    return DT_CFI;
  return NODIRECTIVE;
}

int
immediate(const char **pp, int64_t *value)
{
  const char *p        = *pp;
  int         negative = 0;
  if (*p == '-') {
    negative = 1;
    ++p;
  }

  int base = 10;
  if (*p == '0') {
    char c = tolower(p[1]);
    if (c == 'x') {
      base = 16;
      p += 2;
      c = tolower(*p);
      if (!isxdigit(c))
        return 0;
    } else if (isdigit(c)) {
      if (c >= '8')
        return 0;
      base = 8;
    }
  }
  const char   *q   = p;
  unsigned long val = strtoul(p, (char **)&p, base);
  if (p == q)
    return 0;

  *value = negative ? -val : val;
  *pp    = p;
  return 1;
}

int
is_label_first_chr(char c)
{
  return isalpha(c) || c == '_' || c == '.';
}

int
is_label_chr(char c)
{
  return is_label_first_chr(c) || isdigit(c);
}

static const char *
get_label_end(struct ParseInfo *info)
{
  const char *start = info->p;
  const char *p     = start;
  if (*p == '"') {
    ++p;
    for (;;) {
      char c = *p;
      if (c == '\0') {
        parse_error(info, "string not closed");
        break;
      }

      ++p;
      if (c == '"')
        break;
      if (c == '\\')
        ++p;
    }
    /* a properly closed quoted string is valid even when empty ("");
     * the p <= start check below exists for the bare-identifier
     * branch, where reaching it unmoved means no label was found at
     * all, so it does not apply here */
    return p;
  } else {
    const unsigned char *q   = (const unsigned char *)p;
    int                  ucc = 0;
    for (;;) {
      int uc = *++q;
      if (ucc > 0) {
        if (!isutf8follow(uc)) {
          parse_error(info, "illegal byte sequence");
          return NULL;
        }
        --ucc;
        continue;
      }
      if ((ucc = isutf8first(uc) - 1) > 0)
        continue;
      if (!is_label_chr(uc))
        break;
    }
    p = (const char *)q;
  }
  if (p <= start)
    parse_error(info, "empty label");
  return p;
}

const struct Name *
unquote_label(const char *p, const char *q)
{
  if (*p != '"')
    return alloc_name(p, q, 0);
  if (q[-1] != '"' || q == p + 2)
    return NULL;
  /* TODO: unescape */
  return alloc_name(p + 1, q - 1, 0);
}

static const struct Name *
parse_label(struct ParseInfo *info)
{
  const char *start = info->p;
  const char *p     = get_label_end(info);
  if (p == start)
    return NULL;

  info->p = p;
  return unquote_label(start, p);
}

static const struct Name *
parse_section_name(struct ParseInfo *info)
{
  const char *p     = info->p;
  const char *start = p;

  if (!is_label_first_chr(*p))
    return NULL;

  do {
    ++p;
  } while (isalnum_(*p) || *p == '.' || *p == '-' || *p == '_');
  info->p = p;
  return alloc_name(start, p, 0);
}

enum TokenKind {
  TK_EOF,
  TK_UNKNOWN,
  TK_LABEL,
  TK_FIXNUM,
  TK_ADD,
  TK_SUB,
  TK_MUL,
  TK_DIV,
  TK_FLONUM,
};

struct Token {
  enum TokenKind kind;
  union {
    struct {
      const struct Name *name;
    } label;
    int64_t fixnum;
#ifndef __NO_FLONUM
    Flonum flonum;
#endif
  };
};

static struct Token *
new_token(enum TokenKind kind)
{
  struct Token *token = calloc_or_die(sizeof(*token));
  token->kind         = kind;
  return token;
}

#ifndef __NO_FLONUM
static struct Token *
read_flonum(struct ParseInfo *info, int base)
{
  const char *start = info->p;
  char       *next;
#ifdef __TC
  /* long double in the toolchain compiler is same as double, and if the target platform uses
 */
  /* system library, it makes discrepancy */
  Flonum val = strtod(start, &next);
#else
  Flonum val = strtold(start, &next);
#endif
  struct Token *tok = new_token(TK_FLONUM);
  tok->flonum       = val;
  info->p           = next;

  if (base == 16) {
    /* check exponent part exists */
    const char *q;
    for (q = start; q < next; ++q) {
      if (tolower(*q) == 'p')
        break;
    }
    if (q >= next) {
      parse_error(info, "hex float literal must have exponent part");
    }
  }

  return tok;
}
#endif

static const struct Token *
fetch_token(struct ParseInfo *info)
{
  static const struct Token kTokEOF = {.kind = TK_EOF};
  const char               *start   = skip_whitespaces(info->p);
  const char               *p       = start;
  char                      c       = *p;
  switch (c) {
    case '\0':
      return &kTokEOF;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
      int base = 10;
      if (c == '0' && tolower(p[1]) == 'x') {
#ifndef __NO_FLONUM
        if (c == '.') /* hex float literal */
          return read_flonum(info, 16);
#endif
        if (isxdigit(p[2])) {
          p += 2;
          base = 16;
        }
      }
      char              *q;
      unsigned long long v = strtoull(p, &q, base);
#ifndef __NO_FLONUM
      if (*q == '.' || tolower(*q) == 'e') {
        info->p = p;
        return read_flonum(info, 10);
      }
#endif
      struct Token *token = new_token(TK_FIXNUM);
      token->fixnum       = v;
      info->p             = q;
      return token;
    }
    case '"': {
      info->p                  = p;
      const struct Name *label = parse_label(info);
      if (label != NULL) {
        struct Token *token = new_token(TK_LABEL);
        token->label.name   = label;
        return token;
      }
    } break;
    case '+':
    case '-':
    case '*':
    case '/': {
      enum TokenKind kind;
      switch (c) {
        default:
          assert(0); /* to suppress warning */
        case '+':
          kind = TK_ADD;
          break;
        case '-':
          kind = TK_SUB;
          break;
        case '*':
          kind = TK_MUL;
          break;
        case '/':
          kind = TK_DIV;
          break;
      }
      info->p = p + 1;
      return new_token(kind);
    }
#ifndef __NO_FLONUM
    case '.':
      if (isdigit(p[1])) {
        info->p = p;
        return read_flonum(info, 10);
      }
      break;
#endif
    default:
      break;
  }

  if (is_label_first_chr(c)) {
    const char *label = p;
    while (c = *++p, is_label_chr(c))
      ;
    struct Token *token = new_token(TK_LABEL);
    token->label.name   = alloc_name(label, p, 0);
    info->p             = p;
    return token;
  }

  static const struct Token kTokUnknown = {.kind = TK_UNKNOWN};
  return &kTokUnknown;
}

static const struct Token *
match(struct ParseInfo *info, enum TokenKind kind)
{
  const struct Token *token = info->prefetched;
  if (token == NULL)
    token = fetch_token(info);

  if (token->kind != kind) {
    info->prefetched = token;
    return NULL;
  }
  info->prefetched = NULL;
  return token;
}

struct Expr *
new_expr(enum ExprKind kind)
{
  struct Expr *expr = calloc_or_die(sizeof(*expr));
  expr->kind        = kind;
  return expr;
}

static struct Expr *
prim(struct ParseInfo *info)
{
  struct Expr        *expr = NULL;
  const struct Token *tok;
  if ((tok = match(info, TK_LABEL)) != NULL) {
    expr             = new_expr(EX_LABEL);
    expr->label.name = tok->label.name;
  } else if ((tok = match(info, TK_FIXNUM)) != NULL) {
    expr         = new_expr(EX_FIXNUM);
    expr->fixnum = tok->fixnum;
#ifndef __NO_FLONUM
  } else if ((tok = match(info, TK_FLONUM)) != NULL) {
    expr         = new_expr(EX_FLONUM);
    expr->flonum = tok->flonum;
#endif
  }
  return expr;
}

static struct Expr *
unary(struct ParseInfo *info)
{
  const struct Token *tok;
  if ((tok = match(info, TK_ADD)) != NULL) {
    struct Expr *expr = unary(info);
    if (expr == NULL)
      return NULL;
    switch (expr->kind) {
      case EX_FIXNUM:
      case EX_FLONUM:
        return expr;
      default: {
        struct Expr *op = new_expr(EX_POS);
        op->unary.sub   = expr;
        return op;
      }
    }
  }

  if ((tok = match(info, TK_SUB)) != NULL) {
    struct Expr *expr = unary(info);
    if (expr == NULL)
      return NULL;
    switch (expr->kind) {
      case EX_FIXNUM:
        expr->fixnum = -expr->fixnum;
        return expr;
#ifndef __NO_FLONUM
      case EX_FLONUM:
        expr->flonum = -expr->flonum;
        return expr;
#endif
      default: {
        struct Expr *op = new_expr(EX_NEG);
        op->unary.sub   = expr;
        return op;
      }
    }
  }

  return prim(info);
}

static struct Expr *
parse_mul(struct ParseInfo *info)
{
  struct Expr *expr = unary(info);
  if (expr == NULL)
    return expr;

  const struct Token *tok;
  while ((tok = match(info, TK_MUL)) != NULL || (tok = match(info, TK_DIV)) != NULL) {
    struct Expr *rhs = unary(info);
    if (rhs == NULL) {
      parse_error(info, "expression error");
      break;
    }

    struct Expr *lhs = expr;
    if (lhs->kind == EX_FIXNUM && rhs->kind == EX_FIXNUM) {
      switch (tok->kind) {
        case TK_MUL:
          lhs->fixnum *= rhs->fixnum;
          break;
        case TK_DIV:
          lhs->fixnum /= rhs->fixnum;
          break;
        default:
          assert(0);
          break;
      }
    } else {
      expr = new_expr((enum ExprKind)(tok->kind + (EX_MUL - TK_MUL))); /* assume exprkind is same
 * order with tokenkind */
      expr->bop.lhs = lhs;
      expr->bop.rhs = rhs;
    }
  }
  return expr;
}

static struct Expr *
parse_add(struct ParseInfo *info)
{
  struct Expr *expr = parse_mul(info);
  if (expr == NULL)
    return expr;

  const struct Token *tok;
  while ((tok = match(info, TK_ADD)) != NULL || (tok = match(info, TK_SUB)) != NULL) {
    struct Expr *rhs = parse_mul(info);
    if (rhs == NULL) {
      parse_error(info, "expression error");
      break;
    }

    struct Expr *lhs = expr;
    if (lhs->kind == EX_FIXNUM && rhs->kind == EX_FIXNUM) {
      switch (tok->kind) {
        case TK_ADD:
          lhs->fixnum += rhs->fixnum;
          break;
        case TK_SUB:
          lhs->fixnum -= rhs->fixnum;
          break;
        default:
          assert(0);
          break;
      }
    } else {
      /* assume exprkind is same order with tokenkind */
      expr          = new_expr((enum ExprKind)(tok->kind + (EX_ADD - TK_ADD)));
      expr->bop.lhs = lhs;
      expr->bop.rhs = rhs;
    }
  }
  return expr;
}

struct Expr *
parse_expr(struct ParseInfo *info)
{
  info->prefetched = NULL;
  return parse_add(info);
}

#define R_NOOP 0

static /* enum rawopcode */ int
find_raw_opcode(struct ParseInfo *info)
{
  const char *p     = info->p;
  const char *start = p;

  while (isalnum(*p) || *p == '.')
    ++p;
  if (*p == '\0' || isspace(*p)) {
    size_t n = p - start;
    for (int i = 0;; ++i) {
      const char *name = arch.raw_op_table[i];
      if (name == NULL)
        break;
      size_t len = strlen(name);
      if (n == len && strncasecmp(start, name, n) == 0) {
        info->p = skip_whitespaces(p);
        return i + 1;
      }
    }
  }
  return R_NOOP;
}

static int
parse_inst(struct ParseInfo *info, struct Line *line)
{
  struct Inst inst;
  inst.op     = NOOP;
  inst.prefix = 0;
  for (int i = 0; i < (int)ARRAY_SIZE(inst.opr); ++i)
    inst.opr[i].type = NOOPERAND;

  /* scan for arch-specific instruction prefixes (lock, rep, etc) */
  if (arch.scan_prefix) {
    unsigned char pfx;
    while ((pfx = arch.scan_prefix(info)) != 0)
      inst.prefix |= pfx;
  }

  /* enum rawopcode */ int op = find_raw_opcode(info);
  if (op != R_NOOP) {
    const struct ParseInstTable *pt = &arch.parse_inst_table[op];
    int                          n  = pt->count;
#if __STDC_NO_VLA__
    const struct ParseOpArray **candidates = alloca(n * sizeof(*candidates));
    assert(candidates != NULL);
#else
    const struct ParseOpArray *candidates[n];
#endif
    memcpy(candidates, pt->array, n * sizeof(*candidates));
    for (int i = 0; i < (int)ARRAY_SIZE(inst.opr); ++i) {
      unsigned int opr_flags = 0;
      for (int j = 0; j < n; ++j)
        opr_flags |= candidates[j]->opr_flags[i];
      if (opr_flags == 0)
        break;

      if (i > 0) {
        if (*info->p != ',') {
          if (candidates[0]->opr_flags[i] == 0)
            break;
          parse_error(info, "comma expected");
          return 0; /* error */
        }
        info->p = skip_whitespaces(info->p + 1);
      }

      struct Operand *opr    = &inst.opr[i];
      unsigned int    result = arch.parse_operand(info, opr_flags, opr);
      if (result == 0) {
        parse_error(info, "illegal operand");
        info->p = NULL;
        return 0; /* error */
      }

      for (int j = 0; j < n; ++j) {
        if ((candidates[j]->opr_flags[i] & result) == 0) {
          memmove(&candidates[j], &candidates[j + 1], (n - j - 1) * sizeof(*candidates));
          --n;
          --j;
        }
      }

      info->p = skip_whitespaces(info->p);
    }

    if (n > 0) {
      inst.op = candidates[0]->op;
    } else {
      return parse_error(info, "illegal operand");
    }
    if (*info->p != '\0')
      return parse_error(info, "instruction text after operands");
  } else {
    return parse_error(info, "unknown instruction");
  }

  if (inst.op != NOOP) {
    struct Inst *cloned = calloc_or_die(sizeof(inst));
    *cloned             = inst;
    line->inst          = cloned;
  }
  return 1;
}

void
parse_set_p(struct ParseInfo *info, const char *p)
{
  info->p          = p;
  info->prefetched = NULL;
}

static char
unescape_char(struct ParseInfo *info)
{
  const char *p = info->p;
  char        c = *p++;
  switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
      /* octal, but accepts '8' and '9' */
      int x = 0;
      --p;
      for (int i = 0; i < 3; ++i, ++p) { /* "the numeric code is 3 octal digits." */
        char cc = *p;
        if (!isdigit(cc))
          break;
        x = (x << 3) | (cc - '0');
      }
      info->p = p - 1;
      return x;
    }
    case 'x': {
      int x = 0;
      for (;; ++p) { /* "all trailing hex digits are combined." */
        int v = xvalue(*p);
        if (v < 0)
          break; /* TODO: error */
        x = (x << 4) | v;
      }
      info->p = p - 1;
      return x;
    }
    case 'a':
      return '\a';
    case 'b':
      return '\b';
    case 'f':
      return '\f';
    case 'n':
      return '\n';
    case 'r':
      return '\r';
    case 't':
      return '\t';
    case 'v':
      return '\v';

    default:
      parse_error(info, "illegal escape");
      /* fallthrough */
    case '\'':
    case '"':
    case '\\':
      return c;
  }
}

static size_t
unescape_string(struct ParseInfo *info, char *dst)
{
  size_t len = 0;
  for (; *info->p != '"'; ++info->p, ++len) {
    char c = *info->p;
    if (c == '\0')
      parse_error(info, "string not closed");
    if (c == '\\') {
      ++info->p;
      c = unescape_char(info);
    }
    if (dst != NULL)
      *dst++ = c;
  }
  ++info->p;
  return len;
}

#if TC_TARGET_PLATFORM != TC_PLATFORM_APPLE
static char *
parse_string(struct ParseInfo *info)
{
  const char *start = skip_whitespaces(info->p);
  if (*start != '"')
    return NULL;

  info->p       = start;
  const char *p = get_label_end(info);
  if (p == start)
    return NULL;

  info->p = p;

  const char *s = start + 1, *e = p - 1;
  return strndup(s, e - s);
}

static uint32_t
parse_section_flag(struct ParseInfo *info)
{
  uint32_t flag     = 0;
  char    *flag_str = parse_string(info);
  if (flag_str == NULL) {
    parse_error(info, ".section: flag string expected");
  } else {
    for (char *p = flag_str; *p != '\0'; ++p) {
      switch (*p) {
        case 'a': /* ignore */
          break;
        case 'w':
          flag |= SF_WRITABLE;
          break;
        case 'x':
          flag |= SF_EXECUTABLE;
          break;
        case 'T':
          flag |= SF_TLS;
          break;
        default:
          parse_error(info, ".section: illegal flag character");
          break;
      }
    }
  }
  return flag;
}
#endif

static int
dir_string(struct ParseInfo *info, enum DirectiveType dir)
{
  if (*info->p != '"')
    return parse_error(info, "`\"' expected");
  ++info->p;
  const char *p   = info->p;
  size_t      len = unescape_string(info, NULL);
  if (dir == DT_STRING)
    ++len;
  char *str = calloc_or_die(len);
  info->p   = p; /* again */
  unescape_string(info, str);

  struct SectionInfo *section = info->current_section;
  struct Vector      *irs     = section->irs;
  vec_push(irs, new_ir_data(str, len));
  return 1;
}

static int
dir_comm(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  const struct Name *name = parse_label(info);
  if (name == NULL)
    return parse_error(info, ".comm: label expected");
  info->p = skip_whitespaces(info->p);
  if (*info->p != ',')
    return parse_error(info, ".comm: `,' expected");
  info->p = skip_whitespaces(info->p + 1);
  int64_t size;
  if (!immediate(&info->p, &size) || size <= 0)
    return parse_error(info, ".comm: size expected");

  int64_t align = 0;
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    if (
        !immediate(&info->p, &align) ||
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
        align < 0
#else
        align < 1
#endif
    ) {
      return parse_error(info, ".comm: optional alignment expected");
    }
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
    /* p2align on macos */
    align = 1 << align;
#endif
  }

  struct SectionInfo *section = get_section_info(info, kSecBss, kSegBss, SF_BSS | SF_WRITABLE);
  struct Vector      *irs     = section->irs;
  if (align > 1)
    vec_push(irs, new_ir_align(align));
  vec_push(irs, new_ir_label(name));
  vec_push(irs, new_ir_bss(size));

  struct LabelInfo *label = add_label_table(info->label_table, name, section, 1, 0);
  if (label == NULL)
    return 0;
  label->size  = size;
  label->align = align;
  label->flag |= LF_COMM;
  return 1;
}

static int
dir_zero(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  int64_t num;
  if (!immediate(&info->p, &num))
    return parse_error(info, ".zero: number expected");

  struct SectionInfo *section = info->current_section;
  struct Vector      *irs     = section->irs;
  vec_push(irs, new_ir_zero(num));
  return 1;
}

static int
dir_text(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  set_current_section(info, kSecText, kSegText, SF_EXECUTABLE);
  return 1;
}

static int
dir_data(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  set_current_section(info, kSecData, kSegData, SF_WRITABLE);
  return 1;
}

static int
dir_bss(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  set_current_section(info, kSecBss, kSegBss, SF_BSS | SF_WRITABLE);
  return 1;
}

static int
dir_align(struct ParseInfo *info, enum DirectiveType dir)
{
  int64_t align;
  if (!immediate(&info->p, &align))
    return parse_error(info, ".align: number expected");
  if (dir == DT_P2ALIGN)
    align = 1 << align;

  struct SectionInfo *section = info->current_section;
  struct Vector      *irs     = section->irs;
  vec_push(irs, new_ir_align(align));
  return 1;
}

static int
dir_type(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  const struct Name *name = parse_label(info);
  if (name == NULL)
    return parse_error(info, ".type: label expected");
  if (*info->p != ',')
    return parse_error(info, ".type: `,' expected");
  info->p             = skip_whitespaces(info->p + 1);
  enum LabelKind kind = LK_NONE;
  if (strcmp(info->p, "@function") == 0) {
    kind = LK_FUNC;
    info->p += 9;
  } else if (strcmp(info->p, "@object") == 0) {
    kind = LK_OBJECT;
    info->p += 7;
  } else {
    return parse_error(info, "illegal .type");
  }

  struct SectionInfo *section = info->current_section;
  struct LabelInfo   *label   = add_label_table(info->label_table, name, section, 0, 0);
  if (label != NULL) {
    label->kind = kind;
  }
  return 1;
}

static int
dir_bytes(struct ParseInfo *info, enum DirectiveType dir)
{
  struct Expr *expr = parse_expr(info);
  if (expr == NULL)
    return parse_error(info, "expression expected");

  struct SectionInfo *section = info->current_section;
  struct Vector      *irs     = section->irs;

  assert(expr->kind != EX_FLONUM);
  if (expr->kind == EX_FIXNUM) {
    /* TODO: target endian */
    long           value = expr->fixnum;
    int            size  = 1 << (dir - DT_BYTE);
    unsigned char *buf   = malloc_or_die(size);
    for (int i = 0; i < size; ++i)
      buf[i] = value >> (8 * i);
    vec_push(irs, new_ir_data(buf, size));
  } else {
    vec_push(irs, new_ir_expr((enum IrKind)(IR_EXPR_BYTE + (dir - DT_BYTE)), expr));
  }
  return 1;
}

/* .size is parsed but ignored, the linker computes sizes from section layout */
static int
dir_size(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  /* consume the symbol name and comma */
  parse_label(info);
  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    /* consume the rest of the line as an expression */
    parse_expr(info);
  }
  return 1;
}

#ifndef __NO_FLONUM
static int
dir_float(struct ParseInfo *info, enum DirectiveType dir)
{
  struct Expr *expr = parse_expr(info);
  if (expr == NULL)
    return parse_error(info, "expression expected");

  Flonum value;
  switch (expr->kind) {
    case EX_FIXNUM:
      value = expr->fixnum;
      break;
    case EX_FLONUM:
      value = expr->flonum;
      break;
    default:
      assert(0);
      value = -1;
      break;
  }
  int size;
  switch (dir) {
    default:
      assert(0); /* fallthrough */
    case DT_DOUBLE:
      size = sizeof(double);
      break;
    case DT_FLOAT:
      size = sizeof(float);
      break;
  }
  unsigned char *buf = malloc_or_die(size);
  if (dir == DT_FLOAT) {
    float fval = value;
    memcpy(buf, (void *)&fval, sizeof(fval)); /* TODO: endian */
  } else {
    double dval = value;
    memcpy(buf, (void *)&dval, sizeof(dval)); /* TODO: endian */
  }

  struct SectionInfo *section = info->current_section;
  struct Vector      *irs     = section->irs;
  vec_push(irs, new_ir_data(buf, size));
  return 1;
}
#endif

static int
dir_label_attrib(struct ParseInfo *info, enum DirectiveType dir)
{
  const struct Name *name = parse_label(info);
  if (name == NULL) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%s: label expected", dir == DT_GLOBL ? ".globl" : ".local");
    return parse_error(info, buf);
  }

  struct SectionInfo *section = info->current_section;
  struct LabelInfo   *label = add_label_table(info->label_table, name, section, 0, dir == DT_GLOBL);
  if (label == NULL) {
    ++info->error_count;
  } else {
    if (dir == DT_WEAK)
      label->flag |= LF_WEAK;
  }
  return 1;
}

static int
dir_section(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  const struct Name *name = parse_section_name(info);
  if (name == NULL)
    return parse_error(info, ".section: section name expected");
#if TC_TARGET_PLATFORM != TC_PLATFORM_APPLE
  int         flag = 0;
  const char *p    = skip_whitespaces(info->p);
  if (*p == ',') {
    info->p = p + 1;
    flag    = parse_section_flag(info);
  }

  /* optional third field, elf section type (@progbits, @nobits, @note,
   * ...): this assembler infers the section type from the section
   * name/flags itself, so the field is accepted and discarded rather
   * than fed into set_current_section below */
  p = skip_whitespaces(info->p);
  if (*p == ',') {
    const char *q = p + 1;
    q             = skip_whitespaces(q);
    if (*q == '@') {
      ++q;
      while (isalpha((unsigned char)*q) || isdigit((unsigned char)*q))
        ++q;
      info->p = q;
    }
  }

  char *sectname = strndup(name->chars, name->bytes);
  set_current_section(info, sectname, kSegRodata, flag);
#else
  const char *p = skip_whitespaces(info->p);
  if (*p != ',')
    return parse_error(info, "`,' expected");
  info->p                  = skip_whitespaces(p + 1);
  const struct Name *name2 = parse_section_name(info);
  if (name2 == NULL)
    return parse_error(info, ".section: section name expected");

  int flag = 0;
  p        = skip_whitespaces(info->p);
  if (*p == ',') {
    info->p                    = p + 1;
    const struct Name *modname = parse_section_name(info);
    if (modname != NULL) {
      if (equal_name(modname, alloc_cname("mod_init_funcs"))) {
        flag |= SF_INIT_FUNCS;
      } else if (equal_name(modname, alloc_cname("cstring_literals"))) {
        flag |= SF_CSTRLITERALS;
      }
    }
    if (flag == 0)
      return parse_error(info, ".section: section name expected");
  }

  char *segname  = strndup(name->chars, name->bytes);
  char *sectname = strndup(name2->chars, name2->bytes);
  set_current_section(info, sectname, segname, flag);
#endif
  return 1;
}

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
static int
dir_subsections_via_symbols(struct ParseInfo *info, enum DirectiveType dir)
{
  /* TODO: handle this flag */
  UNUSED(info);
  UNUSED(dir);
  return 1;
}
#endif

/* data directive aliases: .int/.word/.nbyte map to .long/.short/etc */
static int
dir_bytes_alias(struct ParseInfo *info, enum DirectiveType dir)
{
  switch (dir) {
    case DT_INT:
      dir = DT_LONG;
      break; /* .int = 4 bytes */
    case DT_WORD:
      dir = DT_SHORT;
      break; /* .word = 2 bytes */
    case DT_2BYTE:
      dir = DT_SHORT;
      break;
    case DT_4BYTE:
      dir = DT_LONG;
      break;
    case DT_8BYTE:
      dir = DT_QUAD;
      break;
    default:
      break;
  }
  return dir_bytes(info, dir);
}

/* .fill repeat, size, value (gas manual)
 * repeat: number of data units, size: 1/2/4/8, value: fill pattern */
static int
dir_fill(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  int64_t             repeat = 1;
  int64_t             size   = 1;
  int64_t             value  = 0;
  struct SectionInfo *section;
  struct Vector      *irs;
  int64_t             i;

  if (!immediate(&info->p, &repeat))
    return parse_error(info, ".fill: repeat count expected");

  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    if (!immediate(&info->p, &size))
      return parse_error(info, ".fill: size expected");
    if (size != 1 && size != 2 && size != 4 && size != 8)
      return parse_error(info, ".fill: size must be 1, 2, 4, or 8");

    info->p = skip_whitespaces(info->p);
    if (*info->p == ',') {
      info->p = skip_whitespaces(info->p + 1);
      if (!immediate(&info->p, &value))
        return parse_error(info, ".fill: value expected");
    }
  }

  section = info->current_section;
  irs     = section->irs;
  if (value == 0) {
    /* a bss section has no data buffer to hold literal bytes; a
       zero fill is exactly what ir_zero already reserves */
    vec_push(irs, new_ir_zero(repeat * size));
    return 1;
  }
  for (i = 0; i < repeat; ++i) {
    unsigned char *buf = malloc_or_die(size);
    int            j;
    for (j = 0; j < size; ++j)
      buf[j] = value >> (8 * j);
    vec_push(irs, new_ir_data(buf, size));
  }
  return 1;
}

/* .space size[, fill] and .skip alias
 * reserves size bytes filled with fill (default 0) */
static int
dir_space(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  int64_t             size;
  int64_t             fill = 0;
  struct SectionInfo *section;
  struct Vector      *irs;
  unsigned char      *buf;
  int                 i;

  if (!immediate(&info->p, &size))
    return parse_error(info, ".space: size expected");

  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    if (!immediate(&info->p, &fill))
      return parse_error(info, ".space: fill value expected");
  }

  section = info->current_section;
  irs     = section->irs;
  if (fill == 0) {
    /* a bss section has no data buffer to hold literal bytes; a
       zero fill is exactly what ir_zero already reserves */
    vec_push(irs, new_ir_zero(size));
    return 1;
  }
  buf = malloc_or_die(size);
  for (i = 0; i < size; ++i)
    buf[i] = (unsigned char)fill;
  vec_push(irs, new_ir_data(buf, size));
  return 1;
}

/* .set/.equ: symbol assignment. tolerated for now, full evaluation
 * would require registering the symbol in the expression evaluator */
static int
dir_set(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  parse_label(info);
  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    parse_expr(info);
  }
  return 1;
}

/* tolerated directives: consume arguments and discard
 * used for .file, .loc, .ident, .extern, .previous, .cfi_* */
static int
dir_ignore(struct ParseInfo *info, enum DirectiveType dir)
{
  UNUSED(dir);
  UNUSED(info);
  return 1;
}

static inline int
handle_directive(struct ParseInfo *info, enum DirectiveType dir)
{
  typedef int (*DirectiveFunc)(struct ParseInfo *info, enum DirectiveType dir);

  static const DirectiveFunc kDirectiveFuncTable[] = {
      [NODIRECTIVE] = NULL,
      [DT_ASCII]    = dir_string,
      [DT_STRING]   = dir_string,
      [DT_SECTION]  = dir_section,
      [DT_TEXT]     = dir_text,
      [DT_DATA]     = dir_data,
      [DT_BSS]      = dir_bss,
      [DT_ALIGN]    = dir_align,
      [DT_P2ALIGN]  = dir_align,
      [DT_BALIGN]   = dir_align,
      [DT_TYPE]     = dir_type,
      [DT_BYTE]     = dir_bytes,
      [DT_SHORT]    = dir_bytes,
      [DT_LONG]     = dir_bytes,
      [DT_QUAD]     = dir_bytes,
      [DT_COMM]     = dir_comm,
      [DT_ZERO]     = dir_zero,
      [DT_GLOBL]    = dir_label_attrib,
      [DT_LOCAL]    = dir_label_attrib,
      [DT_WEAK]     = dir_label_attrib,
      [DT_EXTERN]   = dir_ignore,
      [DT_INT]      = dir_bytes_alias,
      [DT_WORD]     = dir_bytes_alias,
      [DT_2BYTE]    = dir_bytes_alias,
      [DT_4BYTE]    = dir_bytes_alias,
      [DT_8BYTE]    = dir_bytes_alias,
      [DT_FILL]     = dir_fill,
      [DT_SPACE]    = dir_space,
      [DT_SKIP]     = dir_space,
      [DT_SET]      = dir_set,
      [DT_EQU]      = dir_set,
      [DT_FILE]     = dir_ignore,
      [DT_LOC]      = dir_ignore,
      [DT_IDENT]    = dir_ignore,
      [DT_PREVIOUS] = dir_ignore,
#ifndef __NO_FLONUM
      [DT_FLOAT]  = dir_float,
      [DT_DOUBLE] = dir_float,
#endif
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
      [DT_SUBSECTIONS_VIA_SYMBOLS] = dir_subsections_via_symbols,
#endif
      [DT_SIZE] = dir_size,
      [DT_CFI]  = dir_ignore,
  };

  DirectiveFunc func = kDirectiveFuncTable[dir];
  if (func == NULL)
    return 1;
  return (*func)(info, dir);
}

int
parse_line(struct Line *line, struct ParseInfo *info)
{
  memset(line, 0, sizeof(*line));
  line->label = NULL;
  line->inst  = NULL;
  line->dir   = NODIRECTIVE;

  const char *p = skip_whitespaces(info->p);
  info->p       = p;
  const char *q = get_label_end(info);
  const char *r = skip_whitespaces(q);
  if (*r == ':') {
    const struct Name *label = unquote_label(p, q);
    if (label == NULL)
      return parse_error(info, "illegal label");
    line->label = label;
    info->p = p = skip_whitespaces(r + 1);
  } else if (*p == '.') {
    enum DirectiveType dir = find_directive(p + 1, q - p - 1);
    if (dir == NODIRECTIVE) {
      parse_error(info, "unknown directive");
      return 0;
    }
    line->dir = dir;
    info->p   = r;
    return handle_directive(info, dir);
  }

  if (*p != '\0') {
    info->p = p;
    if (!parse_inst(info, line))
      return 0;
  }
  return 1;
}

struct Value
calc_expr(struct Table *label_table, const struct Expr *expr)
{
  assert(expr != NULL);
  switch (expr->kind) {
    case EX_LABEL:
      return (struct Value){.label = expr->label.name, .offset = 0};
    case EX_FIXNUM:
      return (struct Value){.label = NULL, .offset = expr->fixnum};
    case EX_ADD:
    case EX_SUB:
    case EX_MUL:
    case EX_DIV: {
      struct Value lhs = calc_expr(label_table, expr->bop.lhs);
      struct Value rhs = calc_expr(label_table, expr->bop.rhs);
      if (rhs.label != NULL) {
        if (expr->kind == EX_SUB && lhs.label != NULL) {
          struct LabelInfo *llabel, *rlabel;
          if (table_try_get(label_table, lhs.label, (void **)&llabel)
              && table_try_get(label_table, rhs.label, (void **)&rlabel)) {
            return (struct Value){.label = NULL, .offset = llabel->address - rlabel->address};
          } else {
            error("Unresolved");
          }
        }
        if (expr->kind != EX_ADD || lhs.label != NULL) {
          error("Illegal expression");
        }
        /* offset + label */
        return (struct Value){.label = rhs.label, .offset = lhs.offset + rhs.offset};
      }
      if (lhs.label != NULL) {
        if (expr->kind != EX_ADD) {
          error("Illegal expression");
        }
        /* label + offset */
        return (struct Value){.label = lhs.label, .offset = lhs.offset + rhs.offset};
      }

      assert(lhs.label == NULL && rhs.label == NULL);
      switch (expr->kind) {
        case EX_ADD:
          lhs.offset += rhs.offset;
          break;
        case EX_SUB:
          lhs.offset -= rhs.offset;
          break;
        case EX_MUL:
          lhs.offset *= rhs.offset;
          break;
        case EX_DIV:
          lhs.offset /= rhs.offset;
          break;
        default:
          assert(0);
          break;
      }
      return lhs;
    }

    case EX_POS:
    case EX_NEG: {
      struct Value value = calc_expr(label_table, expr->unary.sub);
      if (value.label != NULL) {
        error("Illegal expression");
      }
      if (expr->kind == EX_NEG)
        value.offset = -value.offset;
      return value;
    }

    default:
      assert(0);
      break;
  }
  return (struct Value){.label = NULL, .offset = 0};
}
