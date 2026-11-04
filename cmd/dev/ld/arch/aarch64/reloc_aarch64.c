/* aarch64 relocation handler for the static linker */

#include "ld_arch.h"

void
ld_aarch64_reloc_apply(struct ld_reloc *r)
{
  unsigned long addr;

  switch (r->type) {
    case R_AARCH64_NONE:
      break;
    case R_AARCH64_ABS64:
      *r->dst += r->val;
      break;
    case R_AARCH64_ABS32:
      *(unsigned int *)r->dst += r->val;
      break;
    case R_AARCH64_ABS16:
      *(unsigned short *)r->dst += r->val;
      break;
    case R_AARCH64_PREL64:
      *r->dst += r->val - r->addr;
      break;
    case R_AARCH64_PREL32:
      *(unsigned int *)r->dst += r->val - r->addr;
      break;
    case R_AARCH64_PREL16:
      *(unsigned short *)r->dst += r->val - r->addr;
      break;
    case R_AARCH64_ADR_PREL_PG_HI21: {
      /* adrp: page offset, clear low 12 bits */
      long          page = (r->val & ~0xfffUL) - (r->addr & ~0xfffUL);
      unsigned int *insn = (unsigned int *)r->dst;
      *insn =
          (*insn & 0x9f00001f) | ((page & 0x3000) << (29 - 12)) | (((page >> 14) & 0x7fff) << 5);
    } break;
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_ADD_ABS_LO12_NC: {
      unsigned int *insn = (unsigned int *)r->dst;
      *insn              = (*insn & 0xffc003ff) | ((r->val & 0xfff) << 10);
    } break;
    case R_AARCH64_LDST16_ABS_LO12_NC: {
      unsigned int *insn = (unsigned int *)r->dst;
      *insn              = (*insn & 0xffc003ff) | (((r->val >> 1) & 0xfff) << 10);
    } break;
    case R_AARCH64_LDST32_ABS_LO12_NC: {
      unsigned int *insn = (unsigned int *)r->dst;
      *insn              = (*insn & 0xffc003ff) | (((r->val >> 2) & 0xfff) << 10);
    } break;
    case R_AARCH64_LDST64_ABS_LO12_NC: {
      unsigned int *insn = (unsigned int *)r->dst;
      *insn              = (*insn & 0xffc003ff) | (((r->val >> 3) & 0xfff) << 10);
    } break;
    case R_AARCH64_JUMP26:
    case R_AARCH64_CALL26: {
      /* b/bl: 26-bit immediate, word-aligned */
      long          off  = (r->val - r->addr) >> 2;
      unsigned int *insn = (unsigned int *)r->dst;
      *insn              = (*insn & 0xfc000000) | (off & 0x03ffffff);
    } break;
    default:
      die("ld: unknown aarch64 relocation type");
  }
}

static int
ld_aarch64_is_got_rel(unsigned int type)
{
  (void)type;
  return 0;
}

const struct ld_arch_backend ld_arch_aarch64 = {
    .em_machine  = EM_AARCH64,
    .reloc_apply = ld_aarch64_reloc_apply,
    .is_got_rel  = ld_aarch64_is_got_rel,
};
