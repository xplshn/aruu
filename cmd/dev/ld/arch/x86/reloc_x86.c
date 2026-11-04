/* x86 (i386) relocation handler for the static linker */

#include "ld_arch.h"

void
ld_x86_reloc_apply(struct ld_reloc *r)
{
  switch (r->type) {
    case R_386_NONE:
      break;
    case R_386_16:
      *(unsigned short *)r->dst += r->val;
      break;
    case R_386_32:
      *(unsigned int *)r->dst += r->val;
      break;
    case R_386_PC32:
    case R_386_PLT32:
      *(unsigned int *)r->dst += r->val - r->addr;
      break;
    default:
      die("ld: unknown i386 relocation type");
  }
}

static int
ld_x86_is_got_rel(unsigned int type)
{
  (void)type;
  return 0;
}

const struct ld_arch_backend ld_arch_x86 = {
    .em_machine  = EM_386,
    .reloc_apply = ld_x86_reloc_apply,
    .is_got_rel  = ld_x86_is_got_rel,
};
