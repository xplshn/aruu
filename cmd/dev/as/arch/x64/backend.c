/* x64 arch backend: ties together tables, parser, encoder, and resolver */

#include "../../../config.h"
#include "arch.h"

/* raw_op_table, parse_inst_table, parse_operand */
#include "inst.h"
#include "parse_asm.h"

/* table and function declarations from x64 backend sources */
extern const char                 *kRawOpTable[];
extern const struct ParseInstTable kParseInstTable[];
extern unsigned int
parse_operand(struct ParseInfo *info, unsigned int opr_flag, struct Operand *operand);
extern void assemble_inst(struct Inst *inst, struct ParseInfo *info, struct Code *code);
extern void make_code(struct Inst *inst, struct Code *code, unsigned char *buf, int len);
extern unsigned char x64_scan_prefix(struct ParseInfo *info);
extern void x64_resolve_code_reloc(
    struct IR          *ir,
    struct SectionInfo *section,
    uint64_t            address,
    uint64_t            start_address,
    struct Table       *label_table,
    struct Vector      *unresolved,
    int                *size_upgraded
);

const struct arch_backend arch = {
    .raw_op_table       = kRawOpTable,
    .parse_inst_table   = kParseInstTable,
    .scan_prefix        = x64_scan_prefix,
    .parse_operand      = parse_operand,
    .assemble_inst      = assemble_inst,
    .make_code          = make_code,
    .resolve_code_reloc = x64_resolve_code_reloc,
};
