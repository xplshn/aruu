/* x64-specific code relocation resolution
 * called from core resolve_relative_address for each IR_CODE node */

#include "../../../config.h"
#include "arch.h"

#include <stdint.h>

#include "inst.h"
#include "parse_asm.h"
#include "table.h"
#include "tcutil.h"

static void
put_value(unsigned char *p, int64_t value, int size)
{
  for (int i = 0; i < size; ++i) {
    *p++ = value;
    value >>= 8;
  }
}

static int
make_jmp_long(struct IR *ir)
{
  if (ir->code.flag & INST_LONG_OFFSET)
    return 0;

  struct Inst *inst = ir->code.inst;
  /* change to long offset and recalculate */
  ir->code.flag |= INST_LONG_OFFSET;
  ir->code.len = 0;
  if (inst->op == JMP_D)
    MAKE_CODE(inst, &ir->code, 0xe9, IM32(-1));
  else
    MAKE_CODE(inst, &ir->code, 0x0f, 0x80 + (inst->op - JO), IM32(-1));
  return 1;
}

static void
resolve_direct_mov(
    struct IR          *ir,
    struct SectionInfo *section,
    uint64_t            address,
    uint64_t            start_address,
    struct Table       *label_table,
    struct Vector      *unresolved
)
{
  struct Inst           *inst  = ir->code.inst;
  struct Operand        *mem   = inst->op == MOV_DR ? &inst->opr[0] : &inst->opr[1];
  struct Reg            *reg   = inst->op == MOV_DR ? &inst->opr[1].reg : &inst->opr[0].reg;
  struct Value           value = calc_expr(label_table, mem->direct.expr);
  struct UnresolvedInfo *info;

  if (value.label == NULL) {
    put_value(ir->code.buf + ir->code.len - 4, value.offset, sizeof(int32_t));
    return;
  }
  info        = malloc_or_die(sizeof(*info));
  info->kind  = inst->op == MOV_DR && reg->size == REG32 ? UNRES_X64_ABS32S : UNRES_X64_ABS32;
  info->label = value.label;
  info->src_section    = section;
  info->offset         = address + ir->code.len - 4 - start_address;
  info->add            = value.offset;
  vec_push(unresolved, info);
}

void
x64_resolve_code_reloc(
    struct IR          *ir,
    struct SectionInfo *section,
    uint64_t            address,
    uint64_t            start_address,
    struct Table       *label_table,
    struct Vector      *unresolved,
    int                *size_upgraded
)
{
  struct Inst *inst = ir->code.inst;
  switch (inst->op) {
    case MOV_DR:
    case MOV_RD:
      resolve_direct_mov(ir, section, address, start_address, label_table, unresolved);
      break;
    case MOV_IR:
      /* this shape loads through a got slot (mov sym@gotpcrel(%rip),
         %reg), not sym's address, so it needs a relocation even when
         sym is local. unlike lea_ir there is no direct-address
         fallback to patch in place */
      if (inst->opr[0].indirect.reg.no == RIP) {
        struct Value value = calc_expr(label_table, inst->opr[0].indirect.offset.expr);
        if (value.label != NULL) {
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind                  = UNRES_X64_GOT_LOAD;
          info->label                 = value.label;
          info->src_section           = section;
          info->offset                = address + 3 - start_address;
          info->add                   = value.offset - 4;
          vec_push(unresolved, info);
        }
      }
      break;
    case ADD_IR:
      /* addq sym@gottpoff(%rip), %reg: the second half of qbe's
         initial-exec tls sequence (movq %fs:0, %reg first). it loads a
         got-resident tp-offset, otherwise the same shape as mov_ir */
      if (inst->opr[0].indirect.reg.no == RIP) {
        struct Value value = calc_expr(label_table, inst->opr[0].indirect.offset.expr);
        if (value.label != NULL) {
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind = (inst->opr[0].indirect.offset.flag & LF_GOTTPOFF) ? UNRES_X64_GOTTPOFF
                                                                          : UNRES_X64_GOT_LOAD;
          info->label        = value.label;
          info->src_section  = section;
          info->offset       = address + 3 - start_address;
          info->add          = value.offset - 4;
          vec_push(unresolved, info);
        }
      }
      break;
    case LEA_IR:
      if (inst->opr[0].indirect.reg.no == RIP
          && inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM) {
        struct Value value = calc_expr(label_table, inst->opr[0].indirect.offset.expr);
        if (value.label != NULL) {
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind                  = UNRES_EXTERN_PC32;
          info->label                 = value.label;
          info->src_section           = section;
          info->offset                = address + 3 - start_address;
          info->add                   = value.offset - 4;
          vec_push(unresolved, info);
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
          put_value(ir->code.buf + 3, value.offset, sizeof(int32_t));
#endif
        } else {
          int64_t offset = value.offset - (address + ir->code.len);
          put_value(ir->code.buf + 3, offset, sizeof(int32_t));
        }
      }
      break;
    case MOV_SR:
      /* r_x86_64_tpoff32: a plain tp-relative displacement, not
         pc-relative, so no address math here. the linker computes it
         once the tls layout is known, like ir_asm.c's unres_abs64
         registration for .quad label */
      if (inst->opr[0].segment.offset.flag & LF_TPOFF) {
        struct Value value = calc_expr(label_table, inst->opr[0].segment.offset.expr);
        int          reloc_offset = ir->code.len - 4;
        struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
        info->kind         = UNRES_X64_TPOFF32;
        info->label        = value.label;
        info->src_section  = section;
        info->offset       = address + reloc_offset - start_address;
        info->add          = value.offset;
        vec_push(unresolved, info);
      }
      break;
    case MOVSD_IX:
    case MOVSS_IX:
    case XORPD_IX:
    case XORPS_IX:
    case COMISD_IX:
    case UCOMISD_IX:
    case COMISS_IX:
    case UCOMISS_IX:
      /* rip-relative with a symbolic offset: disp32 is the last 4
         bytes, but unlike lea_ir these encodings may or may not have a
         rex byte, so derive the offset from the encoded length */
      if (inst->opr[0].indirect.reg.no == RIP
          && inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM) {
        struct Value value        = calc_expr(label_table, inst->opr[0].indirect.offset.expr);
        int          reloc_offset = ir->code.len - 4;
        if (value.label != NULL) {
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind                  = UNRES_EXTERN_PC32;
          info->label                 = value.label;
          info->src_section           = section;
          info->offset                = address + reloc_offset - start_address;
          info->add                   = value.offset - 4;
          vec_push(unresolved, info);
        } else {
          int64_t offset = value.offset - (address + ir->code.len);
          put_value(ir->code.buf + reloc_offset, offset, sizeof(int32_t));
        }
      }
      break;
    case JMP_D:
    case JO:
    case JNO:
    case JB:
    case JAE:
    case JE:
    case JNE:
    case JBE:
    case JA:
    case JS:
    case JNS:
    case JP:
    case JNP:
    case JL:
    case JGE:
    case JLE:
    case JG:
      if (inst->opr[0].type == DIRECT) {
        struct Value value = calc_expr(label_table, inst->opr[0].direct.expr);
        if (value.label != NULL) {
          struct LabelInfo *label_info = table_get(label_table, value.label);
          if (label_info == NULL) {
            *size_upgraded |= make_jmp_long(ir);

            struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
            info->kind                  = UNRES_CALL;
            info->label                 = value.label;
            info->src_section           = section;
            info->offset                = address + 1 - start_address;
            info->add                   = value.offset - 4;
            vec_push(unresolved, info);
            break;
          } else {
            value.offset += label_info->address;
          }
        }

        int64_t offset      = value.offset - (address + ir->code.len);
        int     long_offset = ir->code.flag & INST_LONG_OFFSET;
        if (!long_offset) {
          if (!is_im8(offset)) {
            *size_upgraded |= make_jmp_long(ir);
          } else {
            put_value(ir->code.buf + 1, offset, sizeof(int8_t));
          }
        } else {
          if (!is_im32(offset))
            error("jump offset too far (over 32bit)");

          int d = inst->op == JMP_D ? 1 : 2;
          put_value(ir->code.buf + d, offset, sizeof(int32_t));
        }
      }
      break;
    case CALL_D:
      if (inst->opr[0].type == DIRECT) {
        struct Value value = calc_expr(label_table, inst->opr[0].direct.expr);
        if (value.label != NULL) {
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind                  = UNRES_CALL;
          info->label                 = value.label;
          info->src_section           = section;
          info->offset                = address + 1 - start_address;
          info->add                   = value.offset - 4;
          vec_push(unresolved, info);
          break;
        }
        int64_t offset = value.offset - (address + ir->code.len);
        put_value(ir->code.buf + 1, offset, sizeof(int32_t));
      }
      break;
    default:
      break;
  }
}
