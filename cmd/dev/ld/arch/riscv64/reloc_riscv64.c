/* riscv relocation handler for the static linker */

#include "ld_arch.h"

void
ld_riscv_reloc_apply(struct ld_reloc *r)
{
  unsigned long addr;
  long          off;
  long          hi, lo;
  unsigned int *insn;

  switch (r->type) {
    case R_RISCV_NONE:
    case R_RISCV_RELAX:
      break;
    case R_RISCV_32:
      *(unsigned int *)r->dst += r->val;
      break;
    case R_RISCV_64:
      *r->dst += r->val;
      break;
    case R_RISCV_BRANCH:
      /* conditional branch: b-type encoding */
      off   = r->val - r->addr;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0x01fff07f) | (((off >> 12) & 1) << 31) | (((off >> 5) & 0x3f) << 25)
              | (((off >> 1) & 0xf) << 8) | (((off >> 11) & 1) << 7);
      break;
    case R_RISCV_JAL:
      /* unconditional jump: j-type encoding */
      off   = r->val - r->addr;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0x0000007f) | (((off >> 20) & 1) << 31) | (((off >> 1) & 0x3ff) << 21)
              | (((off >> 11) & 1) << 20) | (((off >> 12) & 0xff) << 12);
      break;
    case R_RISCV_CALL:
    case R_RISCV_CALL_PLT: {
      /* auipc + jalr pair */
      unsigned int *jalr;
      off   = r->val - r->addr;
      hi    = (off + 0x800) >> 12;
      lo    = off - (hi << 12);
      insn  = (unsigned int *)r->dst;
      jalr  = (unsigned int *)(r->dst + 1);
      *insn = (*insn & 0xfff) | ((hi & 0xfffff) << 12);
      *jalr = (*jalr & 0xfffff) | ((lo << 20) & 0xfff00000);
    } break;
    case R_RISCV_PCREL_HI20:
    case R_RISCV_GOT_HI20:
      off   = r->val - r->addr;
      hi    = (off + 0x800) >> 12;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0xfff) | ((hi & 0xfffff) << 12);
      break;
    case R_RISCV_PCREL_LO12_I:
      off   = r->val - r->addr;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0xfffff) | ((off & 0xfff) << 20);
      break;
    case R_RISCV_PCREL_LO12_S:
      off   = r->val - r->addr;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0x01fff07f) | (((off >> 5) & 0x7f) << 25) | ((off & 0x1f) << 7);
      break;
    case R_RISCV_HI20:
      hi    = (r->val + 0x800) >> 12;
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0xfff) | ((hi & 0xfffff) << 12);
      break;
    case R_RISCV_LO12_I:
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0xfffff) | ((r->val & 0xfff) << 20);
      break;
    case R_RISCV_LO12_S:
      insn  = (unsigned int *)r->dst;
      *insn = (*insn & 0x01fff07f) | (((r->val >> 5) & 0x7f) << 25) | ((r->val & 0x1f) << 7);
      break;
    default:
      die("ld: unknown riscv relocation type");
  }
}

static int
ld_riscv_is_got_rel(unsigned int type)
{
  (void)type;
  return 0;
}

const struct ld_arch_backend ld_arch_riscv = {
    .em_machine  = EM_RISCV,
    .reloc_apply = ld_riscv_reloc_apply,
    .is_got_rel  = ld_riscv_is_got_rel,
};
