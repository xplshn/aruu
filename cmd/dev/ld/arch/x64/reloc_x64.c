/* x86_64 relocation handler for the static linker */

#include "ld_arch.h"

#include <limits.h>
#include <stdint.h>

static char *
ld_x64_reloc_name(unsigned int type)
{
  switch (type) {
    case R_X86_64_16:
      return "R_X86_64_16";
    case R_X86_64_32:
      return "R_X86_64_32";
    case R_X86_64_32S:
      return "R_X86_64_32S";
    case R_X86_64_PC32:
      return "R_X86_64_PC32";
    case R_X86_64_PLT32:
      return "R_X86_64_PLT32";
    case R_X86_64_GOTPCREL:
      return "R_X86_64_GOTPCREL";
    case R_X86_64_REX_GOTPCRELX:
      return "R_X86_64_REX_GOTPCRELX";
    case R_X86_64_GOTPCRELX:
      return "R_X86_64_GOTPCRELX";
    case R_X86_64_GOTTPOFF:
      return "R_X86_64_GOTTPOFF";
    case R_X86_64_TPOFF32:
      return "R_X86_64_TPOFF32";
    default:
      return "R_X86_64_UNKNOWN";
  }
}

static void
ld_x64_check_s16_or_u16(struct ld_reloc *r, unsigned long value)
{
  if (value > USHRT_MAX && value < ULONG_MAX - INT16_MAX)
    die_reloc(ld_x64_reloc_name(r->type), value);
}

static void
ld_x64_check_s32(struct ld_reloc *r, unsigned long value)
{
  if (value > INT32_MAX && value < ULONG_MAX - INT32_MAX)
    die_reloc(ld_x64_reloc_name(r->type), value);
}

static void
ld_x64_check_u32(struct ld_reloc *r, unsigned long value)
{
  if (value > UINT32_MAX)
    die_reloc(ld_x64_reloc_name(r->type), value);
}

static void
ld_x64_write_field(unsigned long *dst, unsigned long value, int size)
{
  memcpy(dst, &value, size);
}

void
ld_x64_reloc_apply(struct ld_reloc *r)
{
  unsigned long gotaddr, value;
  int           g;

  switch (r->type) {
    case R_X86_64_NONE:
      break;
    case R_X86_64_16:
      value = r->val;
      ld_x64_check_s16_or_u16(r, value);
      ld_x64_write_field(r->dst, value, sizeof(unsigned short));
      break;
    case R_X86_64_32:
      ld_x64_check_u32(r, r->val);
      ld_x64_write_field(r->dst, r->val, sizeof(unsigned int));
      break;
    case R_X86_64_32S:
      ld_x64_check_s32(r, r->val);
      ld_x64_write_field(r->dst, r->val, sizeof(unsigned int));
      break;
    case R_X86_64_64:
      *r->dst += r->val;
      break;
    case R_X86_64_PC64:
      *r->dst += r->val - r->addr;
      break;
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
      /* plt32 is the same as pc32 for a static linker */
      value = r->val - r->addr;
      ld_x64_check_s32(r, value);
      ld_x64_write_field(r->dst, value, sizeof(unsigned int));
      break;
    case R_X86_64_GOTPCREL:
    case R_X86_64_REX_GOTPCRELX:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_GOTTPOFF:
      /* gottpoff loads through a got slot like any gotpcrel form, but
         the slot holds a tp-relative offset, not an address. find the
         got entry for this symbol, since sym_idx alone is only unique
         within one object's symbol table */
      for (g = 0; g < r->oe->ngot; g++)
        if (r->oe->got[g].sym_idx == r->sym_idx && r->oe->got[g].obj_idx == r->obj_idx)
          break;
      if (g == r->oe->ngot)
        die("ld: missing got entry for gotpcrel relocation");
      gotaddr = r->oe->got_vaddr + g * sizeof(unsigned long);
      /* rip-relative disp32 = G + A - P, the addend corrects for
         trailing bytes (immediates) between the disp32 field and the
         next instruction that rip actually points at */
      value = gotaddr + r->addend - r->addr;
      ld_x64_check_s32(r, value);
      ld_x64_write_field(r->dst, value, sizeof(unsigned int));
      break;
    case R_X86_64_TPOFF32:
      /* tls offset, for static link use symbol value directly */
      ld_x64_check_s32(r, r->val);
      ld_x64_write_field(r->dst, r->val, sizeof(unsigned int));
      break;
    default:
      die("ld: unknown x86_64 relocation type");
  }
}

static int
ld_x64_is_got_rel(unsigned int type)
{
  return type == R_X86_64_GOTPCREL || type == R_X86_64_REX_GOTPCRELX || type == R_X86_64_GOTPCRELX
         || type == R_X86_64_GOTTPOFF;
}

const struct ld_arch_backend ld_arch_x64 = {
    .em_machine  = EM_X86_64,
    .reloc_apply = ld_x64_reloc_apply,
    .is_got_rel  = ld_x64_is_got_rel,
};
