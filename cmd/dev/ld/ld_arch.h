/* ld arch dispatch: per-architecture relocation handlers
 * the linker discovers the target arch from input object e_machine
 * and selects the matching backend at runtime */

#ifndef LD_LD_ARCH_H
#define LD_LD_ARCH_H

#include "ld.h"

/* a single relocation to apply. all fields are set by the core
 * relocation loop before calling the arch handler */
struct ld_reloc {
  unsigned int   type;        /* ELF relocation type (R_* constant) */
  unsigned long  val;         /* resolved symbol value + addend */
  unsigned long *dst;         /* pointer to target bytes in section data */
  unsigned long  addr;        /* virtual address of the relocation site */
  struct outelf *oe;          /* linker state for GOT lookups */
  Elf_Shdr      *target_shdr; /* section header of section being patched */
  int            sym_idx;     /* symbol index in source object */
  int            obj_idx;     /* index of source object in oe->objs */
  long           addend;      /* raw rela addend, unmixed with symval */
};

/* per-arch relocation backend */
struct ld_arch_backend {
  int em_machine; /* EM_* constant */
  /* apply a single relocation. modifies *dst in place */
  void (*reloc_apply)(struct ld_reloc *r);
  /* check if a relocation type requires a got entry */
  int (*is_got_rel)(unsigned int type);
};

/* select the arch backend matching the given e_machine
 * returns NULL for unsupported arches */
const struct ld_arch_backend *ld_arch_select(int em_machine);

/* per-arch backend definitions, one per arch source file */
extern const struct ld_arch_backend ld_arch_x64;
extern const struct ld_arch_backend ld_arch_x86;
extern const struct ld_arch_backend ld_arch_arm;
#ifdef EM_RISCV
extern const struct ld_arch_backend ld_arch_riscv;
#endif
#ifdef EM_AARCH64
extern const struct ld_arch_backend ld_arch_aarch64;
#endif

#endif /* LD_LD_ARCH_H */
