/*
 * ld - static ELF linker
 * based on neatld by ali gholami rudi (ISC license)
 *
 * permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies
 */

#include "ld.h"
#include "ld_arch.h"

#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* config */
static unsigned long sec_vaddr[3] = {0x400000};
static unsigned long sec_laddr[3] = {0x400000};
static int           sec_set[3]   = {1};
static int           secalign     = 16;
static char         *entry        = "_start";
static int           e_machine;
static int           e_flags;
static int           nosyms;
static char         *libdirs[MAXLIBDIRS];
static int           nlibdirs;
static char         *mapfile;

/* runtime-selected arch backend, set when first object is loaded */
static const struct ld_arch_backend *ld_arch;

void
die(char *msg)
{
  fprintf(stderr, "ld: %s\n", msg);
  exit(1);
}

void
die_undef(char *name)
{
  fprintf(stderr, "ld: %s undefined\n", name);
  exit(1);
}

void
die_reloc(char *name, unsigned long value)
{
  fprintf(stderr, "ld: %s value 0x%lx out of range\n", name, value);
  exit(1);
}

void
die_dup(char *name)
{
  fprintf(stderr, "ld: duplicate symbol %s\n", name);
  exit(1);
}

static Elf_Sym *
obj_find(struct obj *obj, char *name)
{
  int i;
  for (i = 0; i < obj->nsyms; i++) {
    Elf_Sym *sym = &obj->syms[i];
    if (ELF_ST_BIND(sym->st_info) == STB_LOCAL || sym->st_shndx == SHN_UNDEF)
      continue;
    if (!strcmp(name, obj->symstr + sym->st_name))
      return sym;
  }
  return NULL;
}

static void
obj_init(struct obj *obj, char *mem)
{
  int i;
  obj->mem   = mem;
  obj->ehdr  = (void *)mem;
  obj->shdr  = (void *)(mem + obj->ehdr->e_shoff);
  obj->shstr = mem + obj->shdr[obj->ehdr->e_shstrndx].sh_offset;
  for (i = 0; i < obj->ehdr->e_shnum; i++) {
    if (obj->shdr[i].sh_type != SHT_SYMTAB)
      continue;
    obj->symstr = mem + obj->shdr[obj->shdr[i].sh_link].sh_offset;
    obj->syms   = (void *)(mem + obj->shdr[i].sh_offset);
    obj->nsyms  = obj->shdr[i].sh_size / sizeof(*obj->syms);
  }
}

static void
outelf_init(struct outelf *oe)
{
  memset(oe, 0, sizeof(*oe));
  oe->ehdr.e_ident[0]  = 0x7f;
  oe->ehdr.e_ident[1]  = 'E';
  oe->ehdr.e_ident[2]  = 'L';
  oe->ehdr.e_ident[3]  = 'F';
  oe->ehdr.e_ident[4]  = ELFCLASS;
  oe->ehdr.e_ident[5]  = ELFDATA2LSB;
  oe->ehdr.e_ident[6]  = EV_CURRENT;
  oe->ehdr.e_type      = ET_EXEC;
  oe->ehdr.e_version   = EV_CURRENT;
  oe->ehdr.e_shstrndx  = SHN_UNDEF;
  oe->ehdr.e_ehsize    = sizeof(oe->ehdr);
  oe->ehdr.e_phentsize = sizeof(oe->phdr[0]);
  oe->ehdr.e_shentsize = sizeof(Elf_Shdr);
}

struct secmap *
outelf_mapping(struct outelf *oe, Elf_Shdr *shdr)
{
  int i;
  for (i = 0; i < oe->nsecs; i++)
    if (oe->secs[i].o_shdr == shdr)
      return &oe->secs[i];
  return NULL;
}

static int
outelf_find(struct outelf *oe, char *name, struct obj **sym_obj, Elf_Sym **sym_sym)
{
  int         i;
  struct obj *strong_obj = NULL;
  struct obj *weak_obj   = NULL;
  Elf_Sym    *strong_sym = NULL;
  Elf_Sym    *weak_sym   = NULL;

  /* a strong definition overrides a weak one whatever the link order.
   * musl libc.a depends on it (weak default in one member, strong
   * implementation in another), so the first match is not enough */
  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    Elf_Sym    *sym;
    if (!(sym = obj_find(obj, name)))
      continue;
    if (ELF_ST_BIND(sym->st_info) != STB_WEAK) {
      if (strong_obj)
        die_dup(name);
      strong_obj = obj;
      strong_sym = sym;
    } else if (!weak_obj) {
      weak_obj = obj;
      weak_sym = sym;
    }
  }
  if (strong_obj) {
    *sym_obj = strong_obj;
    *sym_sym = strong_sym;
    return 0;
  }
  if (weak_obj) {
    *sym_obj = weak_obj;
    *sym_sym = weak_sym;
    return 0;
  }
  return 1;
}

static unsigned long
bss_addr(struct outelf *oe, Elf_Sym *sym)
{
  int i;
  for (i = 0; i < oe->nbss_syms; i++)
    if (oe->bss_syms[i].sym == sym)
      return oe->bss_vaddr + oe->bss_syms[i].off;
  return 0;
}

/* linker-defined symbols for section boundaries, empty for static
 * binaries without constructors */
static int
is_ldsym(char *name)
{
  return !strcmp(name, "__init_array_start") || !strcmp(name, "__init_array_end")
         || !strcmp(name, "__fini_array_start") || !strcmp(name, "__fini_array_end")
         || !strcmp(name, "__preinit_array_start") || !strcmp(name, "__preinit_array_end")
         || !strcmp(name, "__ehdr_start") || !strcmp(name, "__executable_start");
}

static unsigned long
ldsym_val(struct outelf *oe, char *name)
{
  if (!strcmp(name, "__ehdr_start") || !strcmp(name, "__executable_start"))
    return sec_vaddr[I_CS];
  return oe->bss_vaddr + oe->bss_len;
}

static unsigned long
symval(struct outelf *oe, struct obj *obj, Elf_Sym *sym)
{
  struct secmap *sec;
  char          *name = obj ? obj->symstr + sym->st_name : NULL;
  int            s_idx, s_off;

  switch (ELF_ST_TYPE(sym->st_info)) {
    case STT_SECTION:
      if ((sec = outelf_mapping(oe, &obj->shdr[sym->st_shndx]))) {
        if (obj->shdr[sym->st_shndx].sh_flags & SHF_TLS)
          return sec->vaddr - oe->tls_size;
        return sec->vaddr;
      }
      break;
    case STT_NOTYPE:
    case STT_OBJECT:
    case STT_FUNC:
    case STT_TLS:
      /* a weak global stays overridable when the sym entry of this relocation
       * defines it locally (st_shndx != SHN_UNDEF). musl libc.a has the weak
       * default and the strong definition in different members, and every
       * reference must bind to the strong one */
      if (name && *name
          && (sym->st_shndx == SHN_UNDEF || ELF_ST_BIND(sym->st_info) == STB_WEAK)) {
        if (outelf_find(oe, name, &obj, &sym)) {
          if (is_ldsym(name))
            return ldsym_val(oe, name);
          /* weak undefined symbols resolve to 0 */
          if (ELF_ST_BIND(sym->st_info) == STB_WEAK)
            return 0;
          else
            die_undef(name);
        }
      }
      if (sym->st_shndx == SHN_COMMON)
        return bss_addr(oe, sym);
      s_idx = sym->st_shndx;
      s_off = sym->st_value;
      if ((sec = outelf_mapping(oe, &obj->shdr[s_idx]))) {
        /* variant ii: a tls symbol address is tp-relative, so return its offset
         * from the thread pointer instead of a runtime address */
        if (obj->shdr[s_idx].sh_flags & SHF_TLS)
          return sec->vaddr + s_off - oe->tls_size;
        return sec->vaddr + s_off;
      }
  }
  return 0;
}

static unsigned long
outelf_addr(struct outelf *oe, char *name)
{
  struct obj *obj;
  Elf_Sym    *sym;

  if (is_ldsym(name))
    return ldsym_val(oe, name);

  if (outelf_find(oe, name, &obj, &sym))
    die_undef(name);
  return symval(oe, obj, sym);
}

static void
outelf_got_prescan(struct outelf *oe)
{
  int i, j, k, g;
  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    for (j = 0; j < obj->ehdr->e_shnum; j++) {
      Elf_Shdr *rs = &obj->shdr[j];
      Elf_Rela *rels;
      int       nrels;
      if (rs->sh_type != SHT_REL_)
        continue;
      rels  = (Elf_Rela *)(obj->mem + rs->sh_offset);
      nrels = rs->sh_size / sizeof(*rels);
      for (k = 0; k < nrels; k++) {
        Elf_Rela *rel     = (void *)&rels[k];
        int       type    = ELF_R_TYPE(rel->r_info);
        int       sym_idx = ELF_R_SYM(rel->r_info);
        if (!ld_arch || !ld_arch->is_got_rel(type))
          continue;
        /* sym_idx is unique only within its own object, so obj_idx must match
         * too */
        for (g = 0; g < oe->ngot; g++)
          if (oe->got[g].sym_idx == sym_idx && oe->got[g].obj_idx == i)
            break;
        if (g == oe->ngot) {
          if (oe->ngot >= MAXGOT)
            die("ld: too many got entries");
          oe->got[oe->ngot].sym_idx = sym_idx;
          oe->got[oe->ngot].obj_idx = i;
          oe->ngot++;
        }
      }
    }
  }
}

static void
outelf_reloc_sec(struct outelf *oe, int o_idx, int s_idx)
{
  struct obj     *obj        = &oe->objs[o_idx];
  Elf_Shdr       *rel_shdr   = &obj->shdr[s_idx];
  Elf_Rela       *rels       = (Elf_Rela *)(obj->mem + obj->shdr[s_idx].sh_offset);
  Elf_Shdr       *other_shdr = &obj->shdr[rel_shdr->sh_info];
  char           *other      = obj->mem + other_shdr->sh_offset;
  int             nrels      = rel_shdr->sh_size / sizeof(*rels);
  struct secmap  *other_sec  = outelf_mapping(oe, other_shdr);
  int             i;

  /* relocations against a section dropped from the output (eh_frame,
   * notes, debug info) have nothing to apply to */
  if (!other_sec)
    return;

  for (i = 0; i < nrels; i++) {
    Elf_Rela       *rel     = (void *)&rels[i];
    int             sym_idx = ELF_R_SYM(rel->r_info);
    Elf_Sym        *sym     = &obj->syms[sym_idx];
    struct ld_reloc r;
    r.type        = ELF_R_TYPE(rel->r_info);
    r.val         = symval(oe, obj, sym) + REL_ADDEND(rel);
    r.dst         = (unsigned long *)(other + rel->r_offset);
    r.addr        = other_sec->vaddr + rel->r_offset;
    r.oe          = oe;
    r.target_shdr = other_shdr;
    r.sym_idx     = sym_idx;
    r.obj_idx     = o_idx;
    r.addend      = REL_ADDEND(rel);
    ld_arch->reloc_apply(&r);
  }
}

static void
outelf_reloc(struct outelf *oe)
{
  int i, j;
  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    for (j = 0; j < obj->ehdr->e_shnum; j++)
      if (obj->shdr[j].sh_type == SHT_REL_)
        outelf_reloc_sec(oe, i, j);
  }
}

static void
alloc_bss(struct outelf *oe, Elf_Sym *sym)
{
  int n               = oe->nbss_syms++;
  int off             = ALIGN(oe->bss_len, MAX(sym->st_value, 4));
  oe->bss_syms[n].sym = sym;
  oe->bss_syms[n].off = off;
  oe->bss_len         = off + sym->st_size;
}

static void
outelf_bss(struct outelf *oe)
{
  int i, j;
  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    for (j = 0; j < obj->nsyms; j++)
      if (obj->syms[j].st_shndx == SHN_COMMON)
        alloc_bss(oe, &obj->syms[j]);
  }
}

#define SEC_TLS(s)   ((s)->sh_flags & SHF_TLS)
#define SEC_CODE(s)  (!SEC_TLS(s) && ((s)->sh_flags & SHF_EXECINSTR))
#define SEC_BSS(s)   (!SEC_TLS(s) && (s)->sh_type == SHT_NOBITS)
#define SEC_DATA(s)  (!SEC_TLS(s) && !SEC_CODE(s) && !SEC_BSS(s))
#define SEC_TDATA(s) (SEC_TLS(s) && (s)->sh_type != SHT_NOBITS)
#define SEC_TBSS(s)  (SEC_TLS(s) && (s)->sh_type == SHT_NOBITS)

static int
outelf_str(struct outelf *oe, char *s)
{
  int   n = oe->nsymstr;
  char *d = oe->symstr + oe->nsymstr;
  while (*s)
    *d++ = *s++;
  *d++        = '\0';
  oe->nsymstr = d - oe->symstr;
  return n;
}

static void
build_symtab(struct outelf *oe)
{
  int       i, j;
  Elf_Sym  *syms     = oe->syms;
  Elf_Shdr *cs_shdr  = &oe->shdr[++oe->nsh];
  Elf_Shdr *ds_shdr  = &oe->shdr[++oe->nsh];
  Elf_Shdr *bss_shdr = &oe->shdr[++oe->nsh];
  Elf_Shdr *sym_shdr = &oe->shdr[++oe->nsh];
  Elf_Shdr *str_shdr = &oe->shdr[++oe->nsh];
  int       n        = 1;
  int       faddr    = oe->shdr_faddr;

  oe->nsh++;
  outelf_str(oe, "");
  sym_shdr->sh_name = outelf_str(oe, ".symtab");
  str_shdr->sh_name = outelf_str(oe, ".strtab");
  cs_shdr->sh_name  = outelf_str(oe, ".text");
  ds_shdr->sh_name  = outelf_str(oe, ".data");
  bss_shdr->sh_name = outelf_str(oe, ".bss");

  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    for (j = 0; j < obj->nsyms; j++) {
      Elf_Sym *sym  = &obj->syms[j];
      int      type = ELF_ST_TYPE(sym->st_info);
      int      bind = ELF_ST_BIND(sym->st_info);
      char    *name = obj->symstr + sym->st_name;
      if (!*name || bind == STB_LOCAL || sym->st_shndx == SHN_UNDEF)
        continue;
      syms[n].st_name  = outelf_str(oe, name);
      syms[n].st_info  = ELF64_ST_INFO(bind, type);
      syms[n].st_value = symval(oe, obj, sym);
      syms[n].st_size  = sym->st_size;
      syms[n].st_shndx = SHN_ABS;
      n++;
    }
  }
  oe->nsyms = n;

  oe->shdr_faddr = faddr;
  faddr += oe->nsh * sizeof(oe->shdr[0]);
  oe->syms_faddr = faddr;
  faddr += oe->nsyms * sizeof(oe->syms[0]);
  oe->symstr_faddr = faddr;

  oe->ehdr.e_shstrndx = str_shdr - oe->shdr;
  oe->ehdr.e_shoff    = oe->shdr_faddr;
  oe->ehdr.e_shnum    = oe->nsh;

  str_shdr->sh_type   = SHT_STRTAB;
  str_shdr->sh_offset = oe->symstr_faddr;
  str_shdr->sh_size   = oe->nsymstr;

  sym_shdr->sh_type    = SHT_SYMTAB;
  sym_shdr->sh_entsize = sizeof(oe->syms[0]);
  sym_shdr->sh_offset  = oe->syms_faddr;
  sym_shdr->sh_size    = oe->nsyms * sizeof(oe->syms[0]);
  sym_shdr->sh_link    = str_shdr - oe->shdr;
  sym_shdr->sh_info    = 0;

  cs_shdr->sh_type      = SHT_PROGBITS;
  cs_shdr->sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
  cs_shdr->sh_offset    = oe->phdr[I_CS].p_offset;
  cs_shdr->sh_addr      = oe->phdr[I_CS].p_vaddr;
  cs_shdr->sh_addralign = oe->phdr[I_CS].p_align;
  cs_shdr->sh_size      = oe->phdr[I_CS].p_filesz;

  ds_shdr->sh_type      = SHT_PROGBITS;
  ds_shdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
  ds_shdr->sh_offset    = oe->phdr[I_DS].p_offset;
  ds_shdr->sh_addr      = oe->phdr[I_DS].p_vaddr;
  ds_shdr->sh_addralign = oe->phdr[I_DS].p_align;
  ds_shdr->sh_size      = oe->phdr[I_DS].p_filesz;

  bss_shdr->sh_type      = SHT_NOBITS;
  bss_shdr->sh_flags     = SHF_ALLOC | SHF_WRITE;
  bss_shdr->sh_offset    = oe->phdr[I_BSS].p_offset;
  bss_shdr->sh_addr      = oe->phdr[I_BSS].p_vaddr;
  bss_shdr->sh_addralign = oe->phdr[I_BSS].p_align;
  bss_shdr->sh_size      = oe->phdr[I_BSS].p_filesz;
}

static int
secmap_cmp_faddr(const void *a, const void *b)
{
  const struct secmap *const *pa = a, *const *pb = b;
  if ((*pa)->faddr < (*pb)->faddr)
    return -1;
  if ((*pa)->faddr > (*pb)->faddr)
    return 1;
  return 0;
}

static void
outelf_write(struct outelf *oe, int fd)
{
  int             i, ncode;
  long            code_end = -1;
  struct secmap **code_secs;

  oe->ehdr.e_entry = outelf_addr(oe, entry) - sec_vaddr[I_CS] + sec_laddr[I_CS];
  if (!nosyms)
    build_symtab(oe);
  oe->ehdr.e_phnum   = oe->nph;
  oe->ehdr.e_phoff   = sizeof(oe->ehdr);
  oe->ehdr.e_machine = e_machine;
  oe->ehdr.e_flags   = e_flags;

  lseek(fd, 0, SEEK_SET);
  write(fd, &oe->ehdr, sizeof(oe->ehdr));
  write(fd, &oe->phdr, oe->nph * sizeof(oe->phdr[0]));

  /* link_cs groups sections by name (.init, .fini, the rest), not by
   * oe->secs[] order, so the index no longer implies faddr. the gap fill
   * below needs true faddr order */
  code_secs = malloc(oe->nsecs * sizeof(*code_secs));
  ncode     = 0;
  for (i = 0; i < oe->nsecs; i++)
    if (SEC_CODE(oe->secs[i].o_shdr))
      code_secs[ncode++] = &oe->secs[i];
  qsort(code_secs, ncode, sizeof(*code_secs), secmap_cmp_faddr);

  for (i = 0; i < ncode; i++) {
    struct secmap *sec = code_secs[i];
    char          *buf = sec->obj->mem + sec->o_shdr->sh_offset;
    int            len = sec->o_shdr->sh_size;
    /* link_cs aligns code sections but leaves the gap, which reads back as
     * zero and decodes as a valid instruction. fill with int3 so a stray
     * fall-through traps */
    if (code_end >= 0 && (long)sec->faddr > code_end) {
      long           gap = sec->faddr - code_end;
      unsigned char *pad = malloc(gap);
      memset(pad, 0xcc, gap);
      lseek(fd, code_end, SEEK_SET);
      write(fd, pad, gap);
      free(pad);
    }
    code_end = sec->faddr + len;
    lseek(fd, sec->faddr, SEEK_SET);
    write(fd, buf, len);
  }
  free(code_secs);

  for (i = 0; i < oe->nsecs; i++) {
    struct secmap *sec = &oe->secs[i];
    char          *buf = sec->obj->mem + sec->o_shdr->sh_offset;
    int            len = sec->o_shdr->sh_size;
    if (SEC_BSS(sec->o_shdr) || SEC_TBSS(sec->o_shdr) || SEC_CODE(sec->o_shdr))
      continue;
    lseek(fd, sec->faddr, SEEK_SET);
    write(fd, buf, len);
  }
  if (oe->ngot > 0) {
    unsigned long *gotbuf = malloc(oe->ngot * sizeof(unsigned long));
    int            g;
    for (g = 0; g < oe->ngot; g++) {
      struct obj *go = &oe->objs[oe->got[g].obj_idx];
      Elf_Sym    *gs = &go->syms[oe->got[g].sym_idx];
      gotbuf[g]      = symval(oe, go, gs);
    }
    lseek(fd, oe->got_faddr, SEEK_SET);
    write(fd, gotbuf, oe->ngot * sizeof(unsigned long));
    free(gotbuf);
  }
  if (!nosyms) {
    lseek(fd, oe->shdr_faddr, SEEK_SET);
    write(fd, &oe->shdr, oe->nsh * sizeof(oe->shdr[0]));
    lseek(fd, oe->syms_faddr, SEEK_SET);
    write(fd, &oe->syms, oe->nsyms * sizeof(oe->syms[0]));
    lseek(fd, oe->symstr_faddr, SEEK_SET);
    write(fd, &oe->symstr, oe->nsymstr);
  }
}

static void
outelf_add(struct outelf *oe, char *mem)
{
  Elf_Ehdr   *ehdr = (void *)mem;
  Elf_Shdr   *shdr = (void *)(mem + ehdr->e_shoff);
  struct obj *obj;
  int         i;

  if (ehdr->e_type != ET_REL)
    return;
  e_machine = ehdr->e_machine;
  e_flags   = ehdr->e_flags;
  if (ld_arch == NULL) {
    ld_arch = ld_arch_select(e_machine);
    if (ld_arch == NULL)
      die("ld: unsupported architecture");
  } else if (e_machine != ld_arch->em_machine) {
    die("ld: mixed input architectures");
  }
  if (oe->nobjs >= MAXOBJS)
    die("ld: too many objects");
  obj = &oe->objs[oe->nobjs++];
  obj_init(obj, mem);
  for (i = 0; i < obj->nsyms; i++) {
    Elf_Sym    *sym = &obj->syms[i];
    struct obj *sym_obj;
    Elf_Sym    *sym_sym;
    if (ELF_ST_BIND(sym->st_info) == STB_LOCAL || sym->st_shndx == SHN_UNDEF
        || ELF_ST_BIND(sym->st_info) == STB_WEAK)
      continue;
    outelf_find(oe, obj->symstr + sym->st_name, &sym_obj, &sym_sym);
  }
  for (i = 0; i < ehdr->e_shnum; i++) {
    struct secmap *sec;
    if (!(shdr[i].sh_flags & 0x7))
      continue;
    if (oe->nsecs >= MAXSECS)
      die("ld: too many sections");
    sec         = &oe->secs[oe->nsecs++];
    sec->o_shdr = &shdr[i];
    sec->obj    = obj;
  }
}

static int
link_cs(
    struct outelf *oe,
    Elf_Phdr      *phdr,
    unsigned long  faddr,
    unsigned long  vaddr,
    unsigned long  laddr,
    int            len
)
{
  int i, pass;
  /* .init/.fini of all objects are fragments of one function (crti.o
   * prologue, object parts, crtn.o epilogue) and must be byte-adjacent
   * wherever the objects sit in link order. pass 0 lays out every .init,
   * pass 1 every .fini, pass 2 the rest */
  for (pass = 0; pass < 3; pass++) {
    for (i = 0; i < oe->nsecs; i++) {
      struct secmap *sec = &oe->secs[i];
      const char    *name;
      int            alignment, is_init, is_fini;
      if (!SEC_CODE(sec->o_shdr))
        continue;
      name    = sec->obj->shstr + sec->o_shdr->sh_name;
      is_init = !strcmp(name, ".init");
      is_fini = !strcmp(name, ".fini");
      if ((pass == 0 && !is_init) || (pass == 1 && !is_fini)
          || (pass == 2 && (is_init || is_fini)))
        continue;
      alignment  = pass < 2 ? 1 : MAX(sec->o_shdr->sh_addralign, 4);
      len        = ALIGN(vaddr + len, alignment) - vaddr;
      sec->vaddr = vaddr + len;
      sec->faddr = faddr + len;
      len += sec->o_shdr->sh_size;
    }
  }
  phdr->p_type   = PT_LOAD;
  phdr->p_flags  = PF_R | PF_X;
  phdr->p_vaddr  = vaddr;
  phdr->p_paddr  = laddr;
  phdr->p_offset = faddr;
  phdr->p_filesz = len;
  phdr->p_memsz  = len;
  phdr->p_align  = PAGE_SIZE;
  return len;
}

static int
link_ds(
    struct outelf *oe, Elf_Phdr *phdr, unsigned long faddr, unsigned long vaddr, unsigned long laddr
)
{
  int len = 0;
  int i;
  for (i = 0; i < oe->nsecs; i++) {
    struct secmap *sec       = &oe->secs[i];
    int            alignment = MAX(sec->o_shdr->sh_addralign, 4);
    if (!SEC_DATA(sec->o_shdr))
      continue;
    len        = ALIGN(vaddr + len, alignment) - vaddr;
    sec->vaddr = vaddr + len;
    sec->faddr = faddr + len;
    len += sec->o_shdr->sh_size;
  }
  len            = ALIGN(len, 4);
  phdr->p_type   = PT_LOAD;
  phdr->p_flags  = PF_R | PF_W;
  phdr->p_align  = PAGE_SIZE;
  phdr->p_vaddr  = vaddr;
  phdr->p_paddr  = laddr;
  phdr->p_filesz = len;
  phdr->p_memsz  = len;
  phdr->p_offset = faddr;
  return len;
}

static int
link_bss(struct outelf *oe, Elf_Phdr *phdr, unsigned long faddr, unsigned long vaddr, int len)
{
  int i;
  for (i = 0; i < oe->nsecs; i++) {
    struct secmap *sec       = &oe->secs[i];
    int            alignment = MAX(sec->o_shdr->sh_addralign, 4);
    if (!SEC_BSS(sec->o_shdr))
      continue;
    len        = ALIGN(vaddr + len, alignment) - vaddr;
    sec->vaddr = vaddr + len;
    sec->faddr = faddr;
    len += sec->o_shdr->sh_size;
  }
  phdr->p_type   = PT_LOAD;
  phdr->p_flags  = PF_R | PF_W;
  phdr->p_vaddr  = vaddr;
  phdr->p_paddr  = vaddr;
  phdr->p_offset = faddr;
  phdr->p_filesz = 0;
  phdr->p_memsz  = len;
  phdr->p_align  = PAGE_SIZE;
  return len;
}

/* merge every .tdata/.tbss into one tls template. variant ii (x86,
 * x86-64) puts tls variables below the thread pointer, so a symbol at
 * offset off sits at tp - (tls_size - off). tdata comes first (it has
 * file bytes), tbss continues the same offset so the template is
 * contiguous. returns 0 and adds no phdr when there is no tls section */
static int
link_tls(struct outelf *oe, Elf_Phdr *phdr, unsigned long faddr, unsigned long vaddr)
{
  int i, len = 0, filesz, align = 1;

  for (i = 0; i < oe->nsecs; i++) {
    struct secmap *sec = &oe->secs[i];
    int            alignment;
    if (!SEC_TDATA(sec->o_shdr))
      continue;
    alignment  = MAX(sec->o_shdr->sh_addralign, 1);
    align      = MAX(align, alignment);
    len        = ALIGN(len, alignment);
    sec->vaddr = len;
    sec->faddr = faddr + len;
    len += sec->o_shdr->sh_size;
  }
  filesz = ALIGN(len, 4);
  len    = filesz;

  for (i = 0; i < oe->nsecs; i++) {
    struct secmap *sec = &oe->secs[i];
    int            alignment;
    if (!SEC_TBSS(sec->o_shdr))
      continue;
    alignment  = MAX(sec->o_shdr->sh_addralign, 1);
    align      = MAX(align, alignment);
    len        = ALIGN(len, alignment);
    sec->vaddr = len;
    len += sec->o_shdr->sh_size;
  }
  if (len == 0)
    return 0;

  len = ALIGN(len, align);

  phdr->p_type   = PT_TLS;
  phdr->p_flags  = PF_R;
  phdr->p_vaddr  = vaddr;
  phdr->p_paddr  = vaddr;
  phdr->p_offset = faddr;
  phdr->p_filesz = filesz;
  phdr->p_memsz  = len;
  phdr->p_align  = align;

  oe->tls_size   = len;
  oe->tls_filesz = filesz;
  return len;
}

static void
outelf_link(struct outelf *oe)
{
  unsigned long faddr, vaddr, laddr;
  int           len, has_tls;

  len   = ALIGN(sizeof(oe->ehdr) + MAXPHDRS * sizeof(oe->phdr[0]), secalign);
  faddr = len & ~PAGE_MASK;
  vaddr = sec_vaddr[I_CS];
  laddr = sec_laddr[I_CS];
  len   = link_cs(oe, &oe->phdr[0], faddr, vaddr, laddr, len & PAGE_MASK);

  len = ALIGN(faddr + len, secalign) - faddr;
  faddr += len;
  vaddr = sec_set[I_DS] ? sec_vaddr[I_DS] | (faddr & PAGE_MASK) : vaddr + PAGE_SIZE + len;
  laddr = sec_set[I_DS] ? sec_laddr[I_DS] | (faddr & PAGE_MASK) : laddr + PAGE_SIZE + len;
  len   = link_ds(oe, &oe->phdr[1], faddr, vaddr, laddr);

  outelf_got_prescan(oe);

  oe->got_vaddr = vaddr + len;
  oe->got_faddr = faddr + len;
  len += oe->ngot * sizeof(unsigned long);
  len = ALIGN(len, 4);

  /* tls template follows the got inside the data segment. its tbss tail
   * has no file bytes, like bss, so it grows p_memsz but not p_filesz */
  has_tls = link_tls(oe, &oe->phdr[3], faddr + len, vaddr + len) != 0;
  oe->phdr[1].p_filesz = ALIGN(len + oe->tls_filesz, 4);
  len                  = ALIGN(len + oe->tls_size, 4);
  oe->phdr[1].p_memsz  = len;

  len = ALIGN(faddr + len, secalign) - faddr;
  faddr += len;
  vaddr = sec_set[I_BSS] ? sec_vaddr[I_BSS] | (faddr & PAGE_MASK) : vaddr + PAGE_SIZE + len;
  outelf_bss(oe);
  oe->bss_vaddr = vaddr;
  len           = link_bss(oe, &oe->phdr[2], faddr, vaddr, oe->bss_len);

  oe->nph        = has_tls ? 4 : 3;
  oe->shdr_faddr = faddr;
  outelf_reloc(oe);
}

/* archive (.a) handling */
struct arhdr {
  char ar_name[16];
  char ar_date[12];
  char ar_uid[6];
  char ar_gid[6];
  char ar_mode[8];
  char ar_size[10];
  char ar_fmag[2];
};

static int
get_be32(unsigned char *s)
{
  return s[3] | (s[2] << 8) | (s[1] << 16) | (s[0] << 24);
}

static int
sym_undef(struct outelf *oe, char *name)
{
  int i, j;
  int undef = 0;
  for (i = 0; i < oe->nobjs; i++) {
    struct obj *obj = &oe->objs[i];
    for (j = 0; j < obj->nsyms; j++) {
      Elf_Sym *sym = &obj->syms[j];
      if (ELF_ST_BIND(sym->st_info) == STB_LOCAL)
        continue;
      if (strcmp(name, obj->symstr + sym->st_name))
        continue;
      if (sym->st_shndx != SHN_UNDEF)
        return 0;
      undef = 1;
    }
  }
  return undef;
}

static int
outelf_ar_link(struct outelf *oe, char *ar, int base)
{
  char *ar_index;
  char *ar_name;
  int   nsyms = get_be32((void *)ar);
  int   added = 0;
  int   i;

  ar_index = ar + 4;
  ar_name  = ar_index + nsyms * 4;
  for (i = 0; i < nsyms; i++) {
    int   off    = get_be32((void *)(ar_index + i * 4)) + sizeof(struct arhdr);
    char *member = ar - base + off;
    int   j;

    if (!sym_undef(oe, ar_name))
      goto next;

    for (j = 0; j < oe->nobjs; j++)
      if (oe->objs[j].mem == member)
        goto next;

    outelf_add(oe, member);
    added++;
  next:
    ar_name = strchr(ar_name, '\0') + 1;
  }
  return added;
}

static void
outelf_archive(struct outelf *oe, char *ar)
{
  char *beg = ar;

  ar += 8;
  for (;;) {
    struct arhdr *hdr  = (void *)ar;
    char         *name = hdr->ar_name;
    int           size;

    ar += sizeof(*hdr);
    hdr->ar_size[sizeof(hdr->ar_size) - 1] = '\0';
    size                                   = atoi(hdr->ar_size);
    size                                   = (size + 1) & ~1;

    if (name[0] == '/' && name[1] == ' ') {
      while (outelf_ar_link(oe, ar, ar - beg))
        ;
      return;
    }
    if (name[0] == '/' && name[1] == '/' && name[2] == ' ')
      outelf_add(oe, ar);
    ar += size;
  }
}

/* find the archive symbol index for multi-pass linking */
static char *
ar_symtab(char *ar)
{
  ar += 8;
  for (;;) {
    struct arhdr *hdr  = (void *)ar;
    char         *name = hdr->ar_name;
    int           size;

    ar += sizeof(*hdr);
    hdr->ar_size[sizeof(hdr->ar_size) - 1] = '\0';
    size                                   = atoi(hdr->ar_size);
    size                                   = (size + 1) & ~1;

    if (name[0] == '/' && name[1] == ' ')
      return ar;
    ar += size;
  }
}

/* multi-pass archive resolution: iterate over all archives until no
 * new objects are pulled in. handles circular deps between libc and
 * libgcc */
static char *ar_mems[MAXFILES];
static int   nar_mems;

static void
resolve_archives(struct outelf *oe)
{
  int i, changed;

  do {
    changed = 0;
    for (i = 0; i < nar_mems; i++) {
      char *symtab = ar_symtab(ar_mems[i]);
      while (outelf_ar_link(oe, symtab, symtab - ar_mems[i]))
        changed = 1;
    }
  } while (changed);
}

/* file I/O */
static long
filesize(int fd)
{
  struct stat st;
  if (fstat(fd, &st) < 0)
    return -1;
  return st.st_size;
}

static char *
fileread(char *path)
{
  int    fd   = open(path, O_RDONLY);
  long   size;
  char  *buf;
  ssize_t nread;

  if (fd < 0)
    return NULL;
  size = filesize(fd);
  if (size <= 0) {
    close(fd);
    return NULL;
  }
  buf = malloc((size_t)size);
  if (!buf) {
    close(fd);
    return NULL;
  }
  nread = read(fd, buf, (size_t)size);
  close(fd);
  if (nread != (ssize_t)size) {
    free(buf);
    return NULL;
  }
  return buf;
}

static int
is_ar(char *path)
{
  int len = strlen(path);
  return len > 2 && path[len - 2] == '.' && path[len - 1] == 'a';
}

static int
lib_find(char *path, char *lib)
{
  struct stat st;
  int         i;

  if (lib[0] == ':') {
    lib++;
    for (i = 0; i < nlibdirs; i++) {
      sprintf(path, "%s/%s", libdirs[i], lib);
      if (!stat(path, &st))
        return 0;
    }
    return 1;
  }

  for (i = 0; i < nlibdirs; i++) {
    sprintf(path, "%s/lib%s.a", libdirs[i], lib);
    if (!stat(path, &st))
      return 0;
  }
  return 1;
}

static unsigned long
hexnum(char *s)
{
  unsigned long n = 0;
  if (s[0] == '0' && s[1] == 'x')
    s += 2;
  for (; isdigit(*s) || isalpha(*s); s++) {
    n <<= 4;
    n |= isdigit(*s) ? *s - '0' : tolower(*s) - 'a' + 10;
  }
  return n;
}

static void
set_addr(int sec, char *arg)
{
  int   idx = I_CS;
  char *sep = strchr(arg, ':');

  if (sec == 'd')
    idx = I_DS;
  if (sec == 'b')
    idx = I_BSS;
  sec_vaddr[idx] = hexnum(arg);
  sec_laddr[idx] = sep ? hexnum(sep + 1) : sec_vaddr[idx];
  sec_set[idx]   = 1;
}

static void
usage(void)
{
  fprintf(
      stderr,
      "usage: ld [-static] [-o outfile] [-e entry] [-L dir] [-l lib]\n"
      "          [-Ttext addr] [-Tdata addr] [-Tbss addr] [-Map file]\n"
      "          [-s] [-p] [-h name] objfile ...\n"
  );
  exit(1);
}

int
main(int argc, char *argv[])
{
  char                 out[PATHLEN] = "a.out";
  static struct outelf oe;
  char                *mem[MAXFILES];
  int                  nmem = 0;
  int                  fd;
  int                  i = 0;

  if (argc < 2)
    usage();
  outelf_init(&oe);

  while (++i < argc) {
    if (argv[i][0] != '-') {
      mem[nmem++] = fileread(argv[i]);
      if (!mem[nmem - 1])
        die("ld: cannot open object");
      if (is_ar(argv[i])) {
        ar_mems[nar_mems++] = mem[nmem - 1];
        outelf_archive(&oe, mem[nmem - 1]);
      } else
        outelf_add(&oe, mem[nmem - 1]);
      continue;
    }

    if (argv[i][1] == 'l' && argv[i][2] == '\0') {
      char path[PATHLEN];
      if (i + 1 >= argc)
        die("ld: -l missing argument");
      if (lib_find(path, argv[++i]))
        die("ld: cannot find library");
      mem[nmem] = fileread(path);
      if (!mem[nmem])
        die("ld: cannot read library");
      nmem++;
      if (is_ar(path)) {
        ar_mems[nar_mems++] = mem[nmem - 1];
        outelf_archive(&oe, mem[nmem - 1]);
      } else
        outelf_add(&oe, mem[nmem - 1]);
      continue;
    }

    if (argv[i][1] == 'l' && argv[i][2] != '\0') {
      char path[PATHLEN];
      if (lib_find(path, argv[i] + 2))
        die("ld: cannot find library");
      mem[nmem] = fileread(path);
      if (!mem[nmem])
        die("ld: cannot read library");
      nmem++;
      if (is_ar(path)) {
        ar_mems[nar_mems++] = mem[nmem - 1];
        outelf_archive(&oe, mem[nmem - 1]);
      } else
        outelf_add(&oe, mem[nmem - 1]);
      continue;
    }

    if (argv[i][1] == 'L' && argv[i][2] == '\0') {
      if (i + 1 >= argc)
        die("ld: -L missing argument");
      if (nlibdirs >= MAXLIBDIRS)
        die("ld: too many library dirs");
      libdirs[nlibdirs++] = argv[++i];
      continue;
    }

    if (argv[i][1] == 'L' && argv[i][2] != '\0') {
      if (nlibdirs >= MAXLIBDIRS)
        die("ld: too many library dirs");
      libdirs[nlibdirs++] = argv[i] + 2;
      continue;
    }

    if (argv[i][1] == 'o' && argv[i][2] == '\0') {
      if (i + 1 >= argc)
        die("ld: -o missing argument");
      strcpy(out, argv[++i]);
      continue;
    }
    if (argv[i][1] == 'o') {
      strcpy(out, argv[i] + 2);
      continue;
    }

    if ((argv[i][1] == 's' || argv[i][1] == 'S') && argv[i][2] == '\0') {
      nosyms = 1;
      continue;
    }

    if (argv[i][1] == 'g' && argv[i][2] == '\0')
      continue;

    if (argv[i][1] == 'e' && argv[i][2] == '\0') {
      if (i + 1 >= argc)
        die("ld: -e missing argument");
      entry = argv[++i];
      continue;
    }
    if (argv[i][1] == 'e') {
      entry = argv[i] + 2;
      continue;
    }

    if (argv[i][1] == 'p' && argv[i][2] == '\0') {
      secalign = PAGE_SIZE;
      continue;
    }

    /* -ttext/-tdata/-tbss: set segment base addresses */
    if (!strncmp(argv[i], "-Ttext", 6)) {
      char *arg = argv[i][6] ? (argv[i][6] == '=' ? argv[i] + 7 : argv[i] + 6) : argv[++i];
      set_addr('c', arg);
      continue;
    }
    if (!strncmp(argv[i], "-Tdata", 6)) {
      char *arg = argv[i][6] ? (argv[i][6] == '=' ? argv[i] + 7 : argv[i] + 6) : argv[++i];
      set_addr('d', arg);
      continue;
    }
    if (!strncmp(argv[i], "-Tbss", 5)) {
      char *arg = argv[i][5] ? (argv[i][5] == '=' ? argv[i] + 6 : argv[i] + 5) : argv[++i];
      set_addr('b', arg);
      continue;
    }

    /* -T linker script: tolerate (not yet supported) */
    if (argv[i][1] == 'T' && argv[i][2] == '\0') {
      if (i + 1 < argc)
        ++i;
      continue;
    }

    /* -map file: write link map */
    if (!strncmp(argv[i], "-Map", 4)) {
      mapfile = argv[i][4] ? (argv[i][4] == '=' ? argv[i] + 5 : argv[i] + 4) : argv[++i];
      continue;
    }

    /* -m: neatld section address or GNU emulation mode */
    if (argv[i][1] == 'm') {
      /* -melf_x86_64: emulation, tolerate */
      if (argv[i][2] == 'e')
        continue;
      /* -m c=addr or -mc=addr: section address */
      if (argv[i][2] == '\0') {
        if (i + 1 < argc) {
          char *a = argv[i + 1];
          /* elf_* is emulation not section */
          if (a[0] == 'e') {
            ++i;
            continue;
          }
          set_addr(a[0], a[2] == '=' ? a + 3 : a + 1);
          ++i;
        }
        continue;
      }
      {
        char  sec = argv[i][2];
        char *arg = argv[i][3] == '=' ? argv[i] + 4 : (argv[i][3] ? argv[i] + 3 : argv[++i]);
        set_addr(sec, arg);
      }
      continue;
    }

    /* -z takes one argument: -z now, -z relro, etc */
    if (argv[i][1] == 'z') {
      if (argv[i][2] == '\0' && i + 1 < argc)
        ++i;
      continue;
    }

    /* -h name / -soname name: shared lib internal name, tolerate */
    if (argv[i][1] == 'h' && argv[i][2] == '\0') {
      if (i + 1 < argc)
        ++i;
      continue;
    }
    if (!strncmp(argv[i] + 1, "soname", 6)) {
      if (argv[i][7] == '\0' && i + 1 < argc)
        ++i;
      continue;
    }

    /* -u sym: force undefined symbol, tolerate */
    if (argv[i][1] == 'u' && argv[i][2] == '\0') {
      if (i + 1 < argc)
        ++i;
      continue;
    }

    /* -defsym sym=val: tolerate */
    if (!strncmp(argv[i] + 1, "defsym", 6)) {
      if (argv[i][7] == '\0' && i + 1 < argc)
        ++i;
      continue;
    }

    /* -r / --relocatable: partial linking, tolerate for now */
    if (argv[i][1] == 'r' && argv[i][2] == '\0')
      continue;

    /* -n / -N: nmagic, tolerate */
    if ((argv[i][1] == 'n' || argv[i][1] == 'N') && argv[i][2] == '\0')
      continue;

    /* -static / -bstatic: already static, no-op */
    if (!strcmp(argv[i] + 1, "static") || !strcmp(argv[i] + 1, "Bstatic"))
      continue;

    /* -shared / -bdynamic: static only, tolerate */
    if (!strcmp(argv[i] + 1, "shared") || !strcmp(argv[i] + 1, "Bdynamic"))
      continue;

    /* -pie / -no-pie / -fno-pie: tolerate */
    if (!strcmp(argv[i] + 1, "pie") || !strcmp(argv[i] + 1, "no-pie")
        || !strcmp(argv[i] + 1, "fno-pie") || !strcmp(argv[i] + 1, "fpie"))
      continue;

    if (argv[i][1] == 'v' && argv[i][2] == '\0') {
      printf("ld (aruu-dev)\n");
      return 0;
    }

    if (!strcmp(argv[i], "--version")) {
      printf("ld (aruu-dev)\n");
      return 0;
    }

    if (!strcmp(argv[i], "--help"))
      usage();

    /* long options: tolerate (--build-id, --hash-style, etc) */
    if (argv[i][1] == '-')
      continue;

    /* unknown single-char option: warn but continue */
    fprintf(stderr, "ld: unknown option: %s\n", argv[i]);
  }

  resolve_archives(&oe);
  outelf_link(&oe);
  fd = open(out, O_WRONLY | O_TRUNC | O_CREAT, 0755);
  if (fd < 0)
    die("ld: failed to create output");
  outelf_write(&oe, fd);
  close(fd);

  for (i = 0; i < nmem; i++)
    free(mem[i]);
  return 0;
}
