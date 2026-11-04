/* intermediate representation for assembly */

#ifndef AS_IR_ASM_H
#define AS_IR_ASM_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t, int64_t */

#include "asm_code.h" /* struct code */

#define BYTE_SIZE  (1)
#define SHORT_SIZE (2)
#define LONG_SIZE  (4)
#define QUAD_SIZE  (8)

struct Expr;
struct Name;
struct SectionInfo;
struct Table;
struct Vector;

/* unresolved relocation categories. the generic kinds are shared across
 * all arch backends. arch-specific kinds are defined in each archs
 * inst.h starting from UNRES_ARCH_BASE so they dont collide */
enum UnresolvedKind {
  /* arch-neutral */
  UNRES_EXTERN,
  UNRES_EXTERN_PC32,
  UNRES_ABS64,
  UNRES_CALL,
  UNRES_PCREL_HI,
  UNRES_PCREL_LO,
  UNRES_GOT_HI,
  UNRES_GOT_LO,
  /* arch-specific kinds start here */
  UNRES_ARCH_BASE = 0x1000,
};

struct UnresolvedInfo {
  const struct Name  *label;
  uintptr_t           offset;
  struct SectionInfo *src_section;
  int                 add;
  enum UnresolvedKind kind;
};

struct Data {
  size_t         len;
  unsigned char *buf;
};

enum IrKind {
  IR_LABEL,
  IR_CODE,
  IR_DATA,
  IR_BSS,
  IR_ZERO,
  IR_ALIGN,
  IR_EXPR_BYTE,
  IR_EXPR_SHORT,
  IR_EXPR_LONG,
  IR_EXPR_QUAD,
};

struct IR {
  enum IrKind kind;
  union {
    const struct Name *label;
    struct Code        code;
    struct Data        data;
    struct {
      const struct Expr *expr;
      int64_t            addend; /* calculated in resolve_relative_address */
    } expr;
    size_t bss;
    size_t zero;
    int    align;
    int    section;
  };
  uint64_t address;
};

struct IR *new_ir_label(const struct Name *label);
struct IR *new_ir_code(const struct Code *code);
struct IR *new_ir_data(const void *data, size_t size);
struct IR *new_ir_bss(size_t size);
struct IR *new_ir_zero(size_t size);
struct IR *new_ir_align(int align);
struct IR *new_ir_expr(enum IrKind kind, const struct Expr *expr);

int calc_label_address(uint64_t start_address, struct Vector *sections, struct Table *label_table);
int resolve_relative_address(
    struct Vector *sections, struct Table *label_table, struct Vector *unresolved
);
void emit_irs(struct Vector *sections);

#endif /* AS_IR_ASM_H */
