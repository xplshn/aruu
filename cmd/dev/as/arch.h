/* arch dispatch: selects the target architecture backend at compile time
 * a new target adds its inst.h below and its arch directory under arch/
 * core code includes this header instead of reaching into arch/ directly */

#ifndef AS_ARCH_H
#define AS_ARCH_H

#include "../config.h"

/* arch-specific type definitions: struct inst, struct operand, enum opcode
 * each inst.h may include ir_asm.h for UNRES_ARCH_BASE to define
 * arch-specific unresolved kinds */
#if TC_TARGET_ARCH == TC_ARCH_X64
#include "arch/x64/inst.h"
#elif TC_TARGET_ARCH == TC_ARCH_AARCH64
#include "arch/aarch64/inst.h"
#elif TC_TARGET_ARCH == TC_ARCH_RISCV64
#include "arch/riscv64/inst.h"
#else
#error "unsupported architecture"
#endif

#include "asm_code.h" /* struct code */

/* code buffer macros. call through the arch backend function table */

#ifndef MAKE_CODE
#define MAKE_CODE(inst, code, ...)                                                                 \
  do {                                                                                             \
    unsigned char buf[] = {__VA_ARGS__};                                                           \
    arch.make_code(inst, code, buf, sizeof(buf));                                                  \
  } while (0)
#endif

#define IM8(x)  (x)
#define IM16(x) (x), ((x) >> 8)
#define IM32(x) (x), ((x) >> 8), ((x) >> 16), ((x) >> 24)
#define IM64(x)                                                                                    \
  (x), ((x) >> 8), ((x) >> 16), ((x) >> 24), ((x) >> 32), ((x) >> 40), ((x) >> 48), ((x) >> 56)

struct ParseInfo;
struct ParseInstTable;
struct SectionInfo;
struct Table;
struct Vector;

/* per-arch hooks called from arch-neutral core code. each arch backend
 * implements these and exports a single arch struct */
struct arch_backend {
  /* mnemonic string table, null-terminated. indexed by raw opcode */
  const char *const *raw_op_table;
  /* instruction shape table, indexed by raw opcode */
  const struct ParseInstTable *parse_inst_table;

  /* scan for instruction prefix keywords (lock, rep, etc). returns
 * the prefix flag bit or 0 if no prefix found. advances info->p
 * past the keyword and trailing whitespace when a prefix matches */
  unsigned char (*scan_prefix)(struct ParseInfo *info);

  /* parse one operand from the input stream. returns a bitmask of
 * matched operand-shape flags or 0 on failure */
  unsigned int (*parse_operand)(
      struct ParseInfo *info, unsigned int opr_flag, struct Operand *operand
  );

  /* encode a parsed instruction into binary. fills code->buf and code->len */
  void (*assemble_inst)(struct Inst *inst, struct ParseInfo *info, struct Code *code);

  /* low-level code buffer filler used by MAKE_CODE macro */
  void (*make_code)(struct Inst *inst, struct Code *code, unsigned char *buf, int len);

  /* resolve arch-specific relocations for an IR_CODE node. core calls
 * this for every code IR during the resolution pass. sets
 * size_upgraded if a branch was promoted to long form and the
 * fixpoint needs another iteration */
  void (*resolve_code_reloc)(
      struct IR          *ir,
      struct SectionInfo *section,
      uint64_t            address,
      uint64_t            start_address,
      struct Table       *label_table,
      struct Vector      *unresolved,
      int                *size_upgraded
  );
};

extern const struct arch_backend arch;

#endif /* AS_ARCH_H */
