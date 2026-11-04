/* ld arch dispatch: selects the right relocation backend at runtime
 * based on the e_machine field of input object files */

#include "ld_arch.h"

const struct ld_arch_backend *
ld_arch_select(int em_machine)
{
  switch (em_machine) {
    case EM_X86_64:
      return &ld_arch_x64;
    case EM_386:
      return &ld_arch_x86;
    case EM_ARM:
      return &ld_arch_arm;
#ifdef EM_RISCV
    case EM_RISCV:
      return &ld_arch_riscv;
#endif
#ifdef EM_AARCH64
    case EM_AARCH64:
      return &ld_arch_aarch64;
#endif
    default:
      return NULL;
  }
}
