/* ld: shared types and declarations for the static linker
 * included by ld.c and per-arch relocation handlers */

#ifndef LD_LD_H
#define LD_LD_H

#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* section order: text, data, bss */
#define I_CS  0
#define I_DS  1
#define I_BSS 2

#define MAXSECS    (1 << 13)
#define MAXOBJS    (1 << 11)
#define MAXSYMS    (1 << 16)
#define MAXSYMSTR  (1 << 20)
#define PAGE_SIZE  (1 << 12)
#define PAGE_MASK  (PAGE_SIZE - 1)
#define MAXFILES   (1 << 10)
#define MAXPHDRS   4
#define MAXLIBDIRS 32
#define PATHLEN    256
#define MAXGOT     (1 << 16)

#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

/* 64-bit ELF by default, switch to 32-bit on __i386__ */
#ifdef __x86_64__
#define ELFCLASS      ELFCLASS64
#define Elf_Ehdr      Elf64_Ehdr
#define Elf_Phdr      Elf64_Phdr
#define Elf_Shdr      Elf64_Shdr
#define Elf_Sym       Elf64_Sym
#define Elf_Rela      Elf64_Rela
#define SHT_REL_      SHT_RELA
#define REL_ADDEND(r) ((r)->r_addend)
#define ELF_ST_BIND   ELF64_ST_BIND
#define ELF_ST_TYPE   ELF64_ST_TYPE
#define ELF_R_SYM     ELF64_R_SYM
#define ELF_R_TYPE    ELF64_R_TYPE
#define ELF_ST_INFO   ELF64_ST_INFO
#else
#define ELFCLASS      ELFCLASS32
#define Elf_Ehdr      Elf32_Ehdr
#define Elf_Phdr      Elf32_Phdr
#define Elf_Shdr      Elf32_Shdr
#define Elf_Sym       Elf32_Sym
#define Elf_Rela      Elf32_Rel
#define SHT_REL_      SHT_REL
#define REL_ADDEND(r) (0)
#define ELF_ST_BIND   ELF32_ST_BIND
#define ELF_ST_TYPE   ELF32_ST_TYPE
#define ELF_R_SYM     ELF32_R_SYM
#define ELF_R_TYPE    ELF32_R_TYPE
#define ELF_ST_INFO   ELF32_ST_INFO
#endif

struct obj {
  char     *mem;
  Elf_Ehdr *ehdr;
  Elf_Shdr *shdr;
  Elf_Sym  *syms;
  int       nsyms;
  char     *symstr;
  char     *shstr;
};

struct secmap {
  Elf_Shdr     *o_shdr;
  struct obj   *obj;
  unsigned long vaddr;
  unsigned long faddr;
};

struct bss_sym {
  Elf_Sym *sym;
  int      off;
};

struct got_entry {
  int sym_idx;
  int obj_idx;
};

struct outelf {
  Elf_Ehdr      ehdr;
  Elf_Phdr      phdr[MAXPHDRS];
  int           nph;
  struct secmap secs[MAXSECS];
  int           nsecs;
  struct obj    objs[MAXOBJS];
  int           nobjs;

  unsigned long code_addr;

  struct bss_sym bss_syms[MAXSYMS];
  int            nbss_syms;
  unsigned long  bss_vaddr;
  int            bss_len;

  /* got: one entry per GOTPCREL/GOTTPOFF relocation. the entry stores
 * the resolved symbol address (or, for a tls symbol, its tp-relative
 * offset). the relocation displacement points here */
  struct got_entry got[MAXGOT];
  int              ngot;
  unsigned long    got_vaddr;
  unsigned long    got_faddr;

  /* tls template: .tdata/.tbss from every object, merged into one
 * block. a tls section's secmap.vaddr holds its offset from the
 * start of this block (0-based), not a runtime address, since tls
 * variables are addressed tp-relative, not absolutely */
  unsigned long tls_size;
  unsigned long tls_filesz;

  Elf_Shdr      shdr[MAXSECS];
  int           nsh;
  char          symstr[MAXSYMSTR];
  Elf_Sym       syms[MAXSYMS];
  int           nsyms;
  int           nsymstr;
  unsigned long shdr_faddr;
  unsigned long syms_faddr;
  unsigned long symstr_faddr;
};

void die(char *msg);
void die_undef(char *name);
void die_reloc(char *name, unsigned long value);
void die_dup(char *name);

struct secmap *outelf_mapping(struct outelf *oe, Elf_Shdr *shdr);

#endif /* LD_LD_H */
