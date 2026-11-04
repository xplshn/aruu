/* arm (32-bit) relocation handler for the static linker */

#include "ld_arch.h"

void
ld_arm_reloc_apply(struct ld_reloc *r)
{
  unsigned long addr;

  switch (r->type) {
    case R_ARM_NONE:
      break;
    case R_ARM_ABS16:
      *(unsigned short *)r->dst += r->val;
      break;
    case R_ARM_ABS32:
      *(unsigned int *)r->dst += r->val;
      break;
    case R_ARM_REL32:
    case R_ARM_PLT32:
      *(unsigned int *)r->dst += r->val - r->addr;
      break;
    case R_ARM_PC24:
      *r->dst = (*r->dst & 0xff000000) | ((*r->dst + ((r->val - r->addr) >> 2)) & 0x00ffffff);
      break;
    default:
      die("ld: unknown arm relocation type");
  }
}

static int
ld_arm_is_got_rel(unsigned int type)
{
  (void)type;
  return 0;
}

const struct ld_arch_backend ld_arch_arm = {
    .em_machine  = EM_ARM,
    .reloc_apply = ld_arm_reloc_apply,
    .is_got_rel  = ld_arm_is_got_rel,
};
