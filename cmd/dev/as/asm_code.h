/* struct code: the binary output of a single encoded instruction
 * arch-neutral. MAKE_CODE and IM* macros live in arch.h since they
 * call into the arch backend */

#ifndef AS_ASM_CODE_H
#define AS_ASM_CODE_H

#include "../config.h"

struct Inst;

/* max encoded instruction width in bytes */
#if TC_TARGET_ARCH == TC_ARCH_RISCV64
#define ARCH_MAX_CODE_LEN 26
#else
#define ARCH_MAX_CODE_LEN 14
#endif

#define INST_LONG_OFFSET (1 << 0) /* 32bit offset on jump etc */

struct Code {
  struct Inst  *inst;
  char          flag;
  char          len;
  unsigned char buf[ARCH_MAX_CODE_LEN];
};

#endif /* AS_ASM_CODE_H */
