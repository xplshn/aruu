/* parser for assembler */

#ifndef AS_PARSE_ASM_H
#define AS_PARSE_ASM_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* int64_t */

struct DataStorage;
struct Inst;
struct Name;
struct Operand;
struct Table;
struct Token;
struct Vector;

enum DirectiveType {
  NODIRECTIVE,
  DT_ASCII,
  DT_STRING,
  DT_SECTION,
  DT_TEXT,
  DT_DATA,
  DT_BSS,
  DT_ALIGN,
  DT_P2ALIGN,
  DT_BALIGN,
  DT_TYPE,
  DT_BYTE,
  DT_SHORT,
  DT_LONG,
  DT_QUAD,
  DT_COMM,
  DT_ZERO,
  DT_GLOBL,
  DT_LOCAL,
  DT_WEAK,
  DT_EXTERN,
  /* gas integer-data directives. .int/.word and the explicit .nbyte forms
 * are aliases of the sized data emitters but need their own table slots
 * because the directive dispatch is table-indexed */
  DT_INT,
  DT_WORD,
  DT_2BYTE,
  DT_4BYTE,
  DT_8BYTE,
  /* space-filling directives: .fill repeats a sized unit, .space/.skip
 * reserve a byte run. in a nobits section a zero fill reserves address
 * space instead of emitting file bytes */
  DT_FILL,
  DT_SPACE,
  DT_SKIP,
  /* gas compatibility directives */
  DT_SET,      /* .set sym, expr */
  DT_EQU,      /* .equ sym, expr (alias for .set) */
  DT_FILE,     /* .file (debug info, tolerated) */
  DT_LOC,      /* .loc (debug info, tolerated) */
  DT_IDENT,    /* .ident (compiler id string, tolerated) */
  DT_PREVIOUS, /* .previous (return to previous section, tolerated) */
#ifndef __NO_FLONUM
  DT_FLOAT,
  DT_DOUBLE,
#endif
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
  DT_SUBSECTIONS_VIA_SYMBOLS,
#endif
  DT_SIZE,
  DT_CFI, /* .cfi_* prefix, not in table, tolerated */
};

#define SF_EXECUTABLE   (1 << 0)
#define SF_WRITABLE     (1 << 1)
#define SF_BSS          (1 << 2)
#define SF_TLS          (1 << 3)
#define SF_INIT_FUNCS   (1 << 8)
#define SF_CSTRLITERALS (1 << 9)

struct SectionInfo {
  const struct Name *name;
  const char        *segname;
  struct Vector     *irs;
  int                flag;

  int                 index;
  uint64_t            start_address;
  struct DataStorage *ds;
  size_t              align;
  size_t              bss_size;
  uint64_t            offset; /* struct file offset */

  int      rela_count;
  void    *rela_buf;
  uint64_t rela_ofs;
};

struct ParseInfo {
  const char *filename;
  const char *rawline;
  const char *p;
  int         lineno;
  int         error_count;

  const struct Token *prefetched;

  struct Table       *section_infos; /* <struct sectioninfo*> */
  struct Table       *label_table;
  struct Table       *size_expr_table;
  struct SectionInfo *current_section;
};

struct SectionInfo *
set_current_section(struct ParseInfo *info, const char *name, const char *segname, int flag);
struct SectionInfo *
get_section_info(struct ParseInfo *info, const char *name, const char *segname, int flag);

extern const char kSecText[];
extern const char kSecRodata[];
extern const char kSecData[];
extern const char kSecBss[];

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
extern const char kSegText[];
extern const char kSegRodata[];
extern const char kSegData[];
extern const char kSegBss[];
#else
#define kSegText   NULL
#define kSegRodata NULL
#define kSegData   NULL
#define kSegBss    NULL
#endif

struct Line {
  const struct Name *label;
  struct Inst       *inst;
  enum DirectiveType dir;
};

enum ExprKind {
  EX_LABEL,
  EX_FIXNUM,
  EX_POS,
  EX_NEG,
  EX_ADD,
  EX_SUB,
  EX_MUL,
  EX_DIV,
  EX_FLONUM,
};

#ifndef __NO_FLONUM
typedef long double Flonum;
#endif

struct Expr {
  enum ExprKind kind;
  union {
    struct {
      const struct Name *name;
    } label;
    int64_t fixnum;
    struct {
      struct Expr *lhs;
      struct Expr *rhs;
    } bop;
    struct {
      struct Expr *sub;
    } unary;
#ifndef __NO_FLONUM
    Flonum flonum;
#endif
  };
};

int  parse_line(struct Line *line, struct ParseInfo *info);
void parse_set_p(struct ParseInfo *info, const char *p);
int  parse_error(struct ParseInfo *info, const char *message);

struct ParseOpArray {
  /* enum opcode */ int op;
  int                 opr_flags[4 + 1];
};

struct ParseInstTable {
  int                         count;
  const struct ParseOpArray **array;
};

/* table externs and parse_operand are declared via struct arch_backend
 * in arch.h */

int                immediate(const char **pp, int64_t *value);
const struct Name *unquote_label(const char *p, const char *q);
struct Expr       *parse_expr(struct ParseInfo *info);
struct Expr       *new_expr(enum ExprKind kind);

struct Value {
  const struct Name *label;
  int64_t            offset;
};

struct Value calc_expr(struct Table *label_table, const struct Expr *expr);

int is_label_first_chr(char c);
int is_label_chr(char c);

#define LF_GLOBAL   (1 << 0)
#define LF_DEFINED  (1 << 1)
#define LF_REFERRED (1 << 2)
#define LF_COMM     (1 << 3)
#define LF_WEAK     (1 << 4)

enum LabelKind {
  LK_NONE,
  LK_FUNC,
  LK_OBJECT,
};

struct LabelInfo {
  struct SectionInfo *section;
  int                 flag;
  uint64_t            address;
  enum LabelKind      kind;
  int                 size;
  int                 align;
};

struct LabelInfo *add_label_table(
    struct Table       *label_table,
    const struct Name  *label,
    struct SectionInfo *section,
    int                 define,
    int                 global
);

#endif /* AS_PARSE_ASM_H */
