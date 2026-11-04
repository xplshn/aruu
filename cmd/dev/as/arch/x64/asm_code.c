#include "../../../config.h"

#include "arch.h"

#include <assert.h>
#include <stdlib.h> /* exit */
#include <string.h> /* memcpy */

#include "inst.h"
#include "parse_asm.h"
#include "tcutil.h"

#ifndef PUT_CODE
#define PUT_CODE(p, ...)                                                                           \
  do {                                                                                             \
    unsigned char buf[] = {__VA_ARGS__};                                                           \
    memcpy(p, buf, sizeof(buf));                                                                   \
  } while (0)
#endif

static unsigned char *
put_code_filtered(unsigned char *p, const short *buf, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    short c = *buf++;
    if (c >= 0)
      *p++ = c;
  }
  return p;
}

void
make_code(struct Inst *inst, struct Code *code, unsigned char *buf, int len)
{
  assert(code->len + len <= (int)sizeof(code->buf));
  code->inst = inst;
  memcpy(code->buf + code->len, buf, len);
  code->len += len;
}

static inline char
opr_regno(const struct Reg *reg)
{
  return reg->no | (reg->x << 3);
}

static inline int
opr_reg8(const struct Reg *reg)
{
  assert(reg->size == REG8);
  return opr_regno(reg) < 4;
}

static inline int
assemble_error(struct ParseInfo *info, const char *message)
{
  parse_error(info, message);
  return 0;
}

static unsigned char *
put_rex0(unsigned char *p, enum RegSize size, int sno, int dno, unsigned char opcode)
{
  if (size == REG16)
    *p++ = 0x66;
  if (sno >= 8 || dno >= 8 || size == REG64)
    *p++ = 0x40 | ((dno & 8) >> 3) | ((sno & 8) >> 1) | (size != REG64 ? 0 : 8);
  *p++ = opcode;
  return p;
}

#define MAKE_REX0(size, sno, dno, opcode)                                                          \
  (size) == REG16 ? 0x66 : -1,                                                                     \
      ((sno) >= 8 || (dno) >= 8 || ((size) == REG8 && ((sno) >= 4 || (dno) >= 4))                  \
       || (size) == REG64)                                                                         \
          ? (0x40 | (((dno) & 8) >> 3) | (((sno) & 8) >> 1) | ((size) != REG64 ? 0 : 8))           \
          : -1,                                                                                    \
      opcode

static unsigned char *
put_rex1(unsigned char *p, enum RegSize size, int rex_prefix, int dno, unsigned char opcode)
{
  p    = put_rex0(p, size, 0, dno, opcode);
  *p++ = rex_prefix | (dno & 7);
  return p;
}

static unsigned char *
put_rex2(unsigned char *p, enum RegSize size, int sno, int dno, unsigned char opcode)
{
  p    = put_rex0(p, size, sno, dno, opcode);
  *p++ = 0xc0 | ((sno & 7) << 3) | (dno & 7);
  return p;
}

#define MAKE_REX_INDIRECT(size, reg, indirect_reg, opcode, op2, offset)                            \
  MAKE_REX0(size, reg, indirect_reg, (size) == REG8 ? opcode : (unsigned char)((opcode) + 1)),     \
      (((offset) == 0 && indirect_reg != RBP - RAX) ? op2                                          \
       : is_im8(offset)                             ? 0x40                                         \
                                                    : 0x80)                                        \
          | (indirect_reg & 7) | ((reg & 7) << 3),                                                 \
      indirect_reg == RSP - RAX ? 0x24 : -1

#define MAKE_OFFSET(offset, no)                                                                    \
  ((offset) == 0 && no) ? -1 : (offset) & 0xff,                                                    \
      ((offset) == 0 && no) || is_im8(offset) ? -1 : ((offset) >> 8) & 0xff,                       \
      ((offset) == 0 && no) || is_im8(offset) ? -1 : ((offset) >> 16) & 0xff,                      \
      ((offset) == 0 && no) || is_im8(offset) ? -1 : ((offset) >> 24) & 0xff

#define MAKE_IM(size, value)                                                                       \
  (value) & 0xff, size < REG16 ? -1 : ((value) >> 8) & 0xff,                                       \
      size < REG32 ? -1 : ((value) >> 16) & 0xff, size < REG32 ? -1 : ((value) >> 24) & 0xff,      \
      size < REG64 ? -1 : ((value) >> 32) & 0xff, size < REG64 ? -1 : ((value) >> 40) & 0xff,      \
      size < REG64 ? -1 : ((value) >> 48) & 0xff, size < REG64 ? -1 : ((value) >> 56) & 0xff

static unsigned char *
asm_noop(struct Inst *inst, struct Code *code)
{
  UNUSED(inst);
  unsigned char *p = code->buf;
  return p;
}

static unsigned char *
asm_mov_rr(struct Inst *inst, struct Code *code)
{
  unsigned char *p    = code->buf;
  enum RegSize   size = inst->opr[0].reg.size;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x88 : 0x89
  );
  return p;
}

static unsigned char *
asm_mov_imr(struct Inst *inst, struct Code *code)
{
  unsigned char *p    = code->buf;
  enum RegSize   size = inst->opr[1].reg.size;
  if (size == REG64 && is_im32(inst->opr[0].immediate)) {
    int d   = inst->opr[1].reg.no;
    int pre = !inst->opr[1].reg.x ? 0x48 : 0x49;
    MAKE_CODE(inst, code, pre, 0xc7, 0xc0 | d, IM32(inst->opr[0].immediate));
    return code->buf;
  }

  short buf[] = {
      MAKE_REX0(
          size, 0, opr_regno(&inst->opr[1].reg), 0xb0 | (size == REG8 ? 0 : 8) | inst->opr[1].reg.no
      ),
      MAKE_IM(size, inst->opr[0].immediate),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_mov_ir(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect.reg.no != RIP) {
    if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM) {
      long         offset  = inst->opr[0].indirect.offset.expr->fixnum;
      enum RegSize size    = inst->opr[1].reg.size;
      int          sno     = opr_regno(&inst->opr[0].indirect.reg);
      int          dno     = opr_regno(&inst->opr[1].reg);
      int          ofszero = offset == 0 && ((sno & 7) != RBP - RAX);
      short        buf[]   = {
          size == REG16 ? 0x66 : -1,
          (size == REG64 || sno >= 8 || dno >= 8)
              ? 0x40 | (size == REG64 ? 0x08 : 0) | ((dno & 8) >> 1) | ((sno & 8) >> 3)
              : -1,
          0x8a | (size == REG8 ? 0 : 0x01),
          ((dno & 7) << 3) | (sno & 7)
              | (ofszero          ? 0
                 : is_im8(offset) ? 0x40
                                  : 0x80),
          (sno & 7) == RSP - RAX ? 0x24 : -1,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));

      if (ofszero) {
        /* nothing */
      } else if (is_im8(offset)) {
        *p++ = offset;
      } else {
        PUT_CODE(p, IM32(offset));
        p += 4;
      }
      return p;
    }
  } else {
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM) {
      int pre = !inst->opr[1].reg.x ? 0x48 : 0x4c;
      int d   = inst->opr[1].reg.no;
      MAKE_CODE(inst, code, pre, 0x8b, 0x05 | (d << 3), IM32(0));
      return code->buf;
    }
  }
  return NULL;
}

/* reg -> (mem), rex prefix + modrm + sib + disp: shared shape behind
 * mov, xchg, xadd, and cmpxchg storing a register into memory. only
 * the opcode bytes differ between them, xadd/cmpxchg add a 0f escape
 * byte ahead of theirs */
static unsigned char *
assemble_reg_to_mem(
    struct Inst *inst, struct Code *code, int escape, unsigned char op8, unsigned char opw
)
{
  if (inst->opr[1].indirect.offset.expr->kind != EX_FIXNUM || inst->opr[1].indirect.reg.no == RIP)
    return NULL;

  long         offset  = inst->opr[1].indirect.offset.expr->fixnum;
  enum RegSize size    = inst->opr[0].reg.size;
  int          sno     = opr_regno(&inst->opr[0].reg);
  int          dno     = opr_regno(&inst->opr[1].indirect.reg);
  int          ofszero = offset == 0 && ((dno & 7) != RBP - RAX);
  short        buf[]   = {
      size == REG16 ? 0x66 : -1,
      (size == REG64 || sno >= 8 || dno >= 8)
          ? 0x40 | (size == REG64 ? 0x08 : 0) | ((sno & 8) >> 1) | ((dno & 8) >> 3)
          : -1,
      escape ? 0x0f : -1,
      size == REG8 ? op8 : opw,
      ((sno & 7) << 3) | (dno & 7)
          | (ofszero          ? 0
             : is_im8(offset) ? 0x40
                              : 0x80),
      (dno & 7) == RSP - RAX ? 0x24 : -1,
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));

  if (ofszero) {
    /* nothing */
  } else if (is_im8(offset)) {
    *p++ = offset;
  } else {
    PUT_CODE(p, IM32(offset));
    p += 4;
  }
  return p;
}

static unsigned char *
asm_mov_ri(struct Inst *inst, struct Code *code)
{
  return assemble_reg_to_mem(inst, code, 0, 0x88, 0x89);
}

static unsigned char *
asm_mov_iir(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect_with_index.offset->kind == EX_FIXNUM) {
    long offset = inst->opr[0].indirect_with_index.offset->fixnum;
    assert(is_im32(offset));
    short        offset_bit = offset == 0 ? 0x04 : is_im8(offset) ? 0x44 : 0x84;
    enum RegSize size       = inst->opr[1].reg.size;
    assert(inst->opr[0].indirect_with_index.base_reg.no != RIP);
    int          bno        = opr_regno(&inst->opr[0].indirect_with_index.base_reg);
    int          ino        = opr_regno(&inst->opr[0].indirect_with_index.index_reg);
    int          dno        = opr_regno(&inst->opr[1].reg);
    struct Expr *scale_expr = inst->opr[0].indirect_with_index.scale;
    char         scale_bit  = 0;
    if (scale_expr != NULL) {
      int s = scale_expr->fixnum;
      if (s <= 0 || s > 8 || !IS_POWER_OF_2(s))
        return NULL;
      scale_bit = most_significant_bit(s);
    }
    short x      = ((bno & 8) >> 3) | ((ino & 8) >> 2) | ((dno & 8) >> 1);
    short prefix = size == REG16 ? 0x66 : size == REG64 || x != 0 ? 0x48 | x : -1;
    short buf[]  = {
        prefix,
        size == REG8 ? 0x8a : 0x8b,
        ((dno & 7) << 3) | offset_bit,
        (scale_bit << 6) | ((ino & 7) << 3) | (bno & 7),
    };
    unsigned char *p = code->buf;
    p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));

    if (offset != 0) {
      if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else {
        PUT_CODE(p, IM32(offset));
        p += 4;
      }
    }
    return p;
  }
  return NULL;
}

static unsigned char *
assemble_direct_mov(struct Inst *inst, struct Code *code, int load)
{
  struct Reg    *reg    = load ? &inst->opr[1].reg : &inst->opr[0].reg;
  enum RegSize   size   = reg->size;
  int            dno    = opr_regno(reg);
  unsigned char  opcode = load ? 0x8b : 0x89;
  unsigned char *p      = code->buf;

  switch (size) {
    case REG8:
      opcode = load ? 0x8a : 0x88;
      break;
    case REG16:
    case REG32:
    case REG64:
      break;
    default:
      return NULL;
  }

  if (size == REG16)
    *p++ = 0x66;
  if (size == REG64 || dno >= 8)
    *p++ = 0x40 | (size == REG64 ? 0x08 : 0x00) | ((dno & 8) >> 1);
  *p++ = opcode;
  *p++ = (dno & 7) << 3 | 0x04;
  *p++ = 0x25;
  PUT_CODE(p, IM32(0));
  p += 4;
  return p;
}

static unsigned char *
asm_mov_dr(struct Inst *inst, struct Code *code)
{
  return assemble_direct_mov(inst, code, 1);
}

static unsigned char *
asm_mov_rd(struct Inst *inst, struct Code *code)
{
  return assemble_direct_mov(inst, code, 0);
}

static unsigned char *
asm_mov_sr(struct Inst *inst, struct Code *code)
{
  enum RegSize dsize = inst->opr[1].reg.size;
  if (inst->opr[0].segment.reg != FS || (dsize != REG32 && dsize != REG64))
    return NULL;

  struct Expr *offset = inst->opr[0].segment.offset.expr;
  int          tpoff  = inst->opr[0].segment.offset.flag & LF_TPOFF;
  if (offset != NULL && offset->kind != EX_FIXNUM && !tpoff)
    return NULL;

  /* tpoff's value is a link-time tls-layout quantity the assembler
     cannot compute, 0 here is a placeholder patched in by the
     relocation x64_resolve_code_reloc registers below */
  uint64_t      value = (offset != NULL && offset->kind == EX_FIXNUM) ? offset->fixnum : 0;
  unsigned char dno   = opr_regno(&inst->opr[1].reg);
  short         buf[] = {
      0x64,
      dsize == REG64 ? 0x48 : -1,
      0x8b,
      (short)(((dno & 7) << 3) | 0x04),
      0x25,
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  PUT_CODE(p, IM32(value));
  p += 4;
  return p;
}

static unsigned char *
asm_movbwlq_imi(struct Inst *inst, struct Code *code)
{
  long          offset = inst->opr[1].indirect.offset.expr->fixnum;
  unsigned char sno    = 0;
  unsigned char dno    = opr_regno(&inst->opr[1].indirect.reg);
  unsigned char op     = (offset == 0 && (dno & 7) != RBP - RAX) ? 0x00
                         : is_im8(offset)                        ? (unsigned char)0x40
                                                                 : (unsigned char)0x80;
  short         buf[]  = {
      inst->op == MOVW_IMI ? 0x66 : -1,
      (inst->op == MOVQ_IMI || dno >= 8)
          ? 0x40 | (inst->op == MOVQ_IMI ? 0x08 : 0) | ((dno & 8) >> 3) | ((sno & 8) >> 1)
          : -1,
      0xc6 | (inst->op == MOVB_IMI ? 0 : 1),
      op | (dno & 7) | ((sno & 7) << 3),
      (dno & 7) == RSP - RAX ? 0x24 : -1,
  };

  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  if (offset == 0 && (dno & 7) != RBP - RAX) {
    ;
  } else if (is_im8(offset)) {
    *p++ = IM8(offset);
  } else if (is_im32(offset)) {
    PUT_CODE(p, IM32(offset));
    p += 4;
  }

  long value = inst->opr[0].immediate;
  switch (inst->op) {
    case MOVB_IMI:
      *p++ = IM8(value);
      break;
    case MOVW_IMI:
      PUT_CODE(p, IM16(value));
      p += 2;
      break;
    case MOVL_IMI:
    case MOVQ_IMI:
      PUT_CODE(p, IM32(value));
      p += 4;
      break;
    default:
      assert(0);
      break;
  }
  return p;
}

static unsigned char *
asm_movbwlq_imd(struct Inst *inst, struct Code *code)
{
  assert(inst->opr[1].direct.expr->kind == EX_FIXNUM);
  int64_t dst = inst->opr[1].direct.expr->fixnum;
  assert(is_im32(dst));

  short buf[] = {
      inst->op == MOVW_IMD ? 0x66 : -1,
      inst->op == MOVQ_IMD ? 0x48 : -1,
      0xc6 | (inst->op == MOVB_IMD ? 0 : 1),
      0x04,
      0x25,
  };

  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  PUT_CODE(p, IM32(dst));
  p += 4;

  long value = inst->opr[0].immediate;
  switch (inst->op) {
    case MOVB_IMD:
      *p++ = IM8(value);
      break;
    case MOVW_IMD:
      PUT_CODE(p, IM16(value));
      p += 2;
      break;
    case MOVL_IMD:
    case MOVQ_IMD:
      PUT_CODE(p, IM32(value));
      p += 4;
      break;
    default:
      assert(0);
      break;
  }
  return p;
}

static unsigned char *
asm_movszx_rr(struct Inst *inst, struct Code *code)
{
  int            s  = inst->opr[0].reg.no;
  int            d  = inst->opr[1].reg.no;
  unsigned char  op = inst->op == MOVZX ? 0xb6 : 0xbe;
  unsigned char *p  = code->buf;
  switch (inst->opr[0].reg.size) {
    case REG8:
      switch (inst->opr[1].reg.size) {
        case REG16:
          if (opr_reg8(&inst->opr[0].reg) && !inst->opr[1].reg.x) {
            MAKE_CODE(inst, code, 0x66, 0x0f, op, 0xc0 + s + d * 8);
          } else {
            int pre = ((inst->opr[0].reg.x & 1) == 0 ? 0x40 : 0x41) + (!inst->opr[1].reg.x ? 0 : 4);
            MAKE_CODE(inst, code, 0x66, pre, 0x0f, op, 0xc0 + s + d * 8);
          }
          return p;
        case REG32:
          if (opr_reg8(&inst->opr[0].reg) && !inst->opr[1].reg.x) {
            MAKE_CODE(inst, code, 0x0f, op, 0xc0 + s + d * 8);
          } else {
            int pre = ((inst->opr[0].reg.x & 1) == 0 ? 0x40 : 0x41) + (!inst->opr[1].reg.x ? 0 : 4);
            MAKE_CODE(inst, code, pre, 0x0f, op, 0xc0 + s + d * 8);
          }
          return p;
        case REG64: {
          int pre = ((inst->opr[0].reg.x & 1) == 0 ? 0x48 : 0x49) + (!inst->opr[1].reg.x ? 0 : 4);
          MAKE_CODE(inst, code, pre, 0x0f, op, 0xc0 + s + d * 8);
          return p;
        }
        default:
          break;
      }
      break;
    case REG16:
      switch (inst->opr[1].reg.size) {
        case REG32:
          if (!inst->opr[0].reg.x && !inst->opr[1].reg.x) {
            MAKE_CODE(inst, code, 0x0f, op | 1, 0xc0 + s + d * 8);
          } else {
            int pre = (!inst->opr[0].reg.x ? 0x40 : 0x41) + (!inst->opr[1].reg.x ? 0 : 4);
            MAKE_CODE(inst, code, pre, 0x0f, op | 1, 0xc0 + s + d * 8);
          }
          return p;
        case REG64: {
          int pre = (!inst->opr[0].reg.x ? 0x48 : 0x49) + (!inst->opr[1].reg.x ? 0 : 4);
          MAKE_CODE(inst, code, pre, 0x0f, op | 1, 0xc0 + s + d * 8);
          return p;
        }
        default:
          break;
      }
      break;
    case REG32:
      /* "MOVZX %32bit, %64bit" doesnt exist! */
      if (inst->opr[1].reg.size == REG64 && inst->op == MOVSX) {
        int pre = (!inst->opr[0].reg.x ? 0x48 : 0x49) + (!inst->opr[1].reg.x ? 0 : 4);
        MAKE_CODE(inst, code, pre, 0x63, 0xc0 + s + d * 8);
        return p;
      }
      break;
    default:
      break;
  }
  return NULL;
}

static unsigned char *
asm_movsds_xx(struct Inst *inst, struct Code *code, int single)
{
  unsigned char prefix = single ? 0xf3 : 0xf2;
  unsigned char sno    = inst->opr[0].regxmm - XMM0;
  unsigned char dno    = inst->opr[1].regxmm - XMM0;
  short         buf[]  = {
      prefix,
      sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
      0x0f,
      0x10,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_movsd_xx(struct Inst *inst, struct Code *code)
{
  return asm_movsds_xx(inst, code, 0);
}
static unsigned char *
asm_movss_xx(struct Inst *inst, struct Code *code)
{
  return asm_movsds_xx(inst, code, 1);
}

static unsigned char *
asm_movsds_ix(struct Inst *inst, struct Code *code, int single)
{
  long           offset;
  unsigned char  prefix = single ? 0xf3 : 0xf2;
  unsigned char  dno    = inst->opr[1].regxmm - XMM0;
  int            d      = dno & 7;
  unsigned char *p      = code->buf;

  if (inst->opr[0].indirect.reg.no != RIP) {
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM
        || !(offset = inst->opr[0].indirect.offset.expr->fixnum, is_im32(offset)))
      return NULL;
    {
      unsigned char sno = opr_regno(&inst->opr[0].indirect.reg);
      int           s   = sno & 7;
      unsigned char op  = (offset == 0 && s != RBP - RAX) ? (unsigned char)0x00
                          : is_im8(offset)                ? (unsigned char)0x40
                                                          : (unsigned char)0x80;

      short buf[] = {
          prefix,
          sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
          0x0f,
          0x10,
          op | s | (d << 3),
          s == RSP - RAX ? 0x24 : -1,
      };
      p = put_code_filtered(p, buf, ARRAY_SIZE(buf));

      if (offset == 0 && s != RBP - RAX) {
        ;
      } else if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else { /* is_im32(offset) */
        PUT_CODE(p, IM32(offset));
        p += 4;
      }
      return p;
    }
  }

  /* rip-relative with a symbolic offset: emit a placeholder disp32,
     the same convention asm_lea_ir uses, resolved to a pc32
     relocation by the ir pass */
  if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM)
    return NULL;
  {
    short buf[] = {
        prefix,
        dno >= 8 ? (unsigned char)0x44 : -1,
        0x0f,
        0x10,
        0x05 | (d << 3),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    PUT_CODE(p, IM32(0));
    p += 4;
    return p;
  }
}
static unsigned char *
asm_movsd_ix(struct Inst *inst, struct Code *code)
{
  return asm_movsds_ix(inst, code, 0);
}
static unsigned char *
asm_movss_ix(struct Inst *inst, struct Code *code)
{
  return asm_movsds_ix(inst, code, 1);
}

static unsigned char *
asm_movsds_xi(struct Inst *inst, struct Code *code, int single)
{
  long offset;
  if (inst->opr[1].indirect.offset.expr->kind == EX_FIXNUM
      && (offset = inst->opr[1].indirect.offset.expr->fixnum, is_im32(offset))) {
    if (inst->opr[1].indirect.reg.no != RIP) {
      unsigned char prefix = single ? 0xf3 : 0xf2;
      unsigned char sno    = inst->opr[0].regxmm - XMM0;
      unsigned char dno    = opr_regno(&inst->opr[1].indirect.reg);
      int           d      = dno & 7;
      int           s      = sno & 7;
      unsigned char op     = (offset == 0 && d != RBP - RAX) ? (unsigned char)0x00
                             : is_im8(offset)                ? (unsigned char)0x40
                                                             : (unsigned char)0x80;

      short buf[] = {
          prefix,
          sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((dno & 8) >> 3) | ((sno & 8) >> 1) : -1,
          0x0f,
          0x11,
          op | d | (s << 3),
          d == RSP - RAX ? 0x24 : -1,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));

      if (offset == 0 && d != RBP - RAX) {
        ;
      } else if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else { /* is_im32(offset) */
        PUT_CODE(p, IM32(offset));
        p += 4;
      }
      return p;
    }
  }
  return NULL;
}
static unsigned char *
asm_movsd_xi(struct Inst *inst, struct Code *code)
{
  return asm_movsds_xi(inst, code, 0);
}
static unsigned char *
asm_movss_xi(struct Inst *inst, struct Code *code)
{
  return asm_movsds_xi(inst, code, 1);
}

static unsigned char *
assemble_bop_sd(struct Inst *inst, struct Code *code, int single, unsigned char op)
{
  unsigned char *p      = code->buf;
  unsigned char  prefix = single ? 0xf3 : 0xf2;
  if (inst->opr[0].type == REG_XMM && inst->opr[1].type == REG_XMM) {
    unsigned char sno   = inst->opr[0].regxmm - XMM0;
    unsigned char dno   = inst->opr[1].regxmm - XMM0;
    short         buf[] = {
        prefix,
        sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
        0x0f,
        op,
        (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  }

  return p;
}

static unsigned char *
asm_addsd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 0, 0x58);
}
static unsigned char *
asm_addss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 1, 0x58);
}

static unsigned char *
asm_subsd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 0, 0x5c);
}
static unsigned char *
asm_subss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 1, 0x5c);
}

static unsigned char *
asm_mulsd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 0, 0x59);
}
static unsigned char *
asm_mulss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 1, 0x59);
}

static unsigned char *
asm_divsd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 0, 0x5e);
}
static unsigned char *
asm_divss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_bop_sd(inst, code, 1, 0x5e);
}

static unsigned char *
assemble_xorpd_xx(struct Inst *inst, struct Code *code, int single)
{
  unsigned char *p = code->buf;
  assert(inst->opr[0].type == REG_XMM);
  assert(inst->opr[1].type == REG_XMM);
  unsigned char sno   = inst->opr[0].regxmm - XMM0;
  unsigned char dno   = inst->opr[1].regxmm - XMM0;
  short         buf[] = {
      single ? -1 : 0x66,
      sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
      0x0f,
      0x57,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_xorpd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_xorpd_xx(inst, code, 0);
}
static unsigned char *
asm_xorps_xx(struct Inst *inst, struct Code *code)
{
  return assemble_xorpd_xx(inst, code, 1);
}

static unsigned char *
assemble_xorpd_ix(struct Inst *inst, struct Code *code, int single)
{
  long           offset;
  unsigned char  dno = inst->opr[1].regxmm - XMM0;
  int            d   = dno & 7;
  unsigned char *p   = code->buf;

  if (inst->opr[0].indirect.reg.no != RIP) {
    unsigned char sno;
    int           s;
    unsigned char op;
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM
        || !(offset = inst->opr[0].indirect.offset.expr->fixnum, is_im32(offset)))
      return NULL;
    sno = opr_regno(&inst->opr[0].indirect.reg);
    s   = sno & 7;
    op  = (offset == 0 && s != RBP - RAX) ? (unsigned char)0x00
          : is_im8(offset)                ? (unsigned char)0x40
                                          : (unsigned char)0x80;
    {
      short buf[] = {
          single ? -1 : 0x66,
          sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
          0x0f,
          0x57,
          op | s | (d << 3),
          s == RSP - RAX ? 0x24 : -1,
      };
      p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    }
    if (offset == 0 && s != RBP - RAX) {
      ;
    } else if (is_im8(offset)) {
      *p++ = IM8(offset);
    } else { /* is_im32(offset) */
      PUT_CODE(p, IM32(offset));
      p += 4;
    }
    return p;
  }

  /* rip-relative with a symbolic offset, same convention as
     asm_movsds_ix: placeholder disp32, resolved by the ir pass */
  if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM)
    return NULL;
  {
    short buf[] = {
        single ? -1 : 0x66,
        dno >= 8 ? (unsigned char)0x44 : -1,
        0x0f,
        0x57,
        0x05 | (d << 3),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    PUT_CODE(p, IM32(0));
    p += 4;
    return p;
  }
}
static unsigned char *
asm_xorpd_ix(struct Inst *inst, struct Code *code)
{
  return assemble_xorpd_ix(inst, code, 0);
}
static unsigned char *
asm_xorps_ix(struct Inst *inst, struct Code *code)
{
  return assemble_xorpd_ix(inst, code, 1);
}

static unsigned char *
assemble_ucomisd(struct Inst *inst, struct Code *code, unsigned char opc, int single)
{
  unsigned char *p = code->buf;
  assert(inst->opr[0].type == REG_XMM);
  assert(inst->opr[1].type == REG_XMM);
  unsigned char sno   = inst->opr[0].regxmm - XMM0;
  unsigned char dno   = inst->opr[1].regxmm - XMM0;
  short         buf[] = {
      single ? -1 : 0x66,
      sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
      0x0f,
      opc,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_comisd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd(inst, code, 0x2f, 0);
}
static unsigned char *
asm_comiss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd(inst, code, 0x2f, 1);
}
static unsigned char *
asm_ucomisd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd(inst, code, 0x2e, 0);
}
static unsigned char *
asm_ucomiss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd(inst, code, 0x2e, 1);
}

/* comisd/ucomisd reg, (mem): same shape as assemble_xorpd_ix with the
 * opcode byte as a parameter instead of fixed at 0x57. qbe emits it to
 * compare a register against a float literal in a rip-relative
 * constant pool */
static unsigned char *
assemble_ucomisd_ix(struct Inst *inst, struct Code *code, unsigned char opc, int single)
{
  long           offset;
  unsigned char  dno = inst->opr[1].regxmm - XMM0;
  int            d   = dno & 7;
  unsigned char *p   = code->buf;

  if (inst->opr[0].indirect.reg.no != RIP) {
    unsigned char sno;
    int           s;
    unsigned char op;
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM
        || !(offset = inst->opr[0].indirect.offset.expr->fixnum, is_im32(offset)))
      return NULL;
    sno = opr_regno(&inst->opr[0].indirect.reg);
    s   = sno & 7;
    op  = (offset == 0 && s != RBP - RAX) ? (unsigned char)0x00
          : is_im8(offset)                ? (unsigned char)0x40
                                          : (unsigned char)0x80;
    {
      short buf[] = {
          single ? -1 : 0x66,
          sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
          0x0f,
          opc,
          op | s | (d << 3),
          s == RSP - RAX ? 0x24 : -1,
      };
      p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    }
    if (offset == 0 && s != RBP - RAX) {
      ;
    } else if (is_im8(offset)) {
      *p++ = IM8(offset);
    } else {
      PUT_CODE(p, IM32(offset));
      p += 4;
    }
    return p;
  }

  /* rip-relative with a symbolic offset, same convention as
     asm_movsds_ix/assemble_xorpd_ix: placeholder disp32, resolved by
     the ir pass */
  if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM)
    return NULL;
  {
    short buf[] = {
        single ? -1 : 0x66,
        dno >= 8 ? (unsigned char)0x44 : -1,
        0x0f,
        opc,
        0x05 | (d << 3),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    PUT_CODE(p, IM32(0));
    p += 4;
    return p;
  }
}
static unsigned char *
asm_comisd_ix(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd_ix(inst, code, 0x2f, 0);
}
static unsigned char *
asm_comiss_ix(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd_ix(inst, code, 0x2f, 1);
}
static unsigned char *
asm_ucomisd_ix(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd_ix(inst, code, 0x2e, 0);
}
static unsigned char *
asm_ucomiss_ix(struct Inst *inst, struct Code *code)
{
  return assemble_ucomisd_ix(inst, code, 0x2e, 1);
}

static unsigned char *
assemble_cvtsi2sd(struct Inst *inst, struct Code *code, int single)
{
  unsigned char *p      = code->buf;
  unsigned char  prefix = single ? 0xf3 : 0xf2;
  unsigned char  sno    = opr_regno(&inst->opr[0].reg);
  unsigned char  dno    = inst->opr[1].regxmm - XMM0;
  short          buf[]  = {
      prefix,
      sno >= 8 || dno >= 8 || inst->opr[0].reg.size == REG64
          ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1)
                | (inst->opr[0].reg.size == REG64 ? 8 : 0)
          : -1,
      0x0f,
      0x2a,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_cvtsi2sd_rx(struct Inst *inst, struct Code *code)
{
  return assemble_cvtsi2sd(inst, code, 0);
}
static unsigned char *
asm_cvtsi2ss_rx(struct Inst *inst, struct Code *code)
{
  return assemble_cvtsi2sd(inst, code, 1);
}

static unsigned char *
assemble_cvttsd2si(struct Inst *inst, struct Code *code, int single)
{
  unsigned char *p      = code->buf;
  unsigned char  prefix = single ? 0xf3 : 0xf2;
  unsigned char  sno    = inst->opr[0].regxmm - XMM0;
  unsigned char  dno    = opr_regno(&inst->opr[1].reg);
  short          buf[]  = {
      prefix,
      sno >= 8 || dno >= 8 || inst->opr[1].reg.size == REG64
          ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1)
                | (inst->opr[1].reg.size == REG64 ? 8 : 0)
          : -1,
      0x0f,
      0x2c,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_cvttsd2si_xr(struct Inst *inst, struct Code *code)
{
  return assemble_cvttsd2si(inst, code, 0);
}
static unsigned char *
asm_cvttss2si_xr(struct Inst *inst, struct Code *code)
{
  return assemble_cvttsd2si(inst, code, 1);
}

static unsigned char *
assemble_cvtsd2ss(struct Inst *inst, struct Code *code, int single)
{
  unsigned char *p      = code->buf;
  unsigned char  prefix = single ? 0xf3 : 0xf2;
  unsigned char  sno    = inst->opr[0].regxmm - XMM0;
  unsigned char  dno    = inst->opr[1].regxmm - XMM0;
  short          buf[]  = {
      prefix,
      sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
      0x0f,
      0x5a,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}
static unsigned char *
asm_cvtsd2ss_xx(struct Inst *inst, struct Code *code)
{
  return assemble_cvtsd2ss(inst, code, 0);
}
static unsigned char *
asm_cvtss2sd_xx(struct Inst *inst, struct Code *code)
{
  return assemble_cvtsd2ss(inst, code, 1);
}

static unsigned char *
asm_sqrtsd_xx(struct Inst *inst, struct Code *code)
{
  unsigned char sno   = inst->opr[0].regxmm - XMM0;
  unsigned char dno   = inst->opr[1].regxmm - XMM0;
  short         buf[] = {
      0xf2,
      sno >= 8 || dno >= 8 ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) : -1,
      0x0f,
      0x51,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* movq %gp64, %xmm: bitwise move, the gp register's 64 bits become
 * the xmm register's low 64 bits (high 64 zeroed). qbe emits this to
 * spill a double through a gp register and to build the sign-bit mask
 * operand xorpd needs for float negation */
static unsigned char *
asm_movq_gpxmm(struct Inst *inst, struct Code *code)
{
  unsigned char sno   = opr_regno(&inst->opr[0].reg);
  unsigned char dno   = inst->opr[1].regxmm - XMM0;
  short         buf[] = {
      0x66,
      (unsigned char)0x48 | ((sno & 8) >> 3) | ((dno & 8) >> 1),
      0x0f,
      0x6e,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* mirror of asm_movq_gpxmm: the xmm register's low 64 bits become the
 * gp register's value */
static unsigned char *
asm_movq_xmmgp(struct Inst *inst, struct Code *code)
{
  unsigned char sno   = inst->opr[0].regxmm - XMM0;
  unsigned char dno   = opr_regno(&inst->opr[1].reg);
  short         buf[] = {
      0x66,
      (unsigned char)0x48 | ((dno & 8) >> 3) | ((sno & 8) >> 1),
      0x0f,
      0x7e,
      (unsigned char)0xc0 | ((sno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_movq_ixmm(struct Inst *inst, struct Code *code)
{
  struct Expr   *offset_expr;
  struct Expr   *scale_expr = NULL;
  long           offset;
  int            bno;
  int            ino = 0;
  int            dno;
  int            d;
  int            mod;
  int            scale_bit = 0;
  int            scale;
  int            indexed = 0;
  short          buf[7];
  unsigned char *p;

  dno = inst->opr[1].regxmm - XMM0;
  d   = dno & 7;
  if (inst->opr[0].type == INDIRECT) {
    bno         = opr_regno(&inst->opr[0].indirect.reg);
    offset_expr = inst->opr[0].indirect.offset.expr;
  } else if (inst->opr[0].type == INDIRECT_WITH_INDEX) {
    bno         = opr_regno(&inst->opr[0].indirect_with_index.base_reg);
    ino         = opr_regno(&inst->opr[0].indirect_with_index.index_reg);
    offset_expr = inst->opr[0].indirect_with_index.offset;
    scale_expr  = inst->opr[0].indirect_with_index.scale;
    indexed     = 1;
  } else {
    return NULL;
  }
  if (bno == RIP || offset_expr->kind != EX_FIXNUM
      || !(offset = offset_expr->fixnum, is_im32(offset)))
    return NULL;
  if (scale_expr != NULL) {
    if (scale_expr->kind != EX_FIXNUM)
      return NULL;
    scale = scale_expr->fixnum;
    if (scale <= 0 || scale > 8 || !IS_POWER_OF_2(scale))
      return NULL;
    scale_bit = most_significant_bit(scale);
  }

  mod    = offset == 0 && (bno & 7) != RBP - RAX ? 0x00 : is_im8(offset) ? 0x40 : 0x80;
  buf[0] = 0xf3;
  buf[1] = bno >= 8 || ino >= 8 || dno >= 8
               ? 0x40 | ((bno & 8) >> 3) | ((ino & 8) >> 2) | ((dno & 8) >> 1)
               : -1;
  buf[2] = 0x0f;
  buf[3] = 0x7e;
  buf[4] = mod | (d << 3) | (indexed ? 0x04 : bno & 7);
  buf[5] = indexed                  ? (scale_bit << 6) | ((ino & 7) << 3) | (bno & 7)
           : (bno & 7) == RSP - RAX ? 0x24
                                    : -1;
  buf[6] = -1;

  p = put_code_filtered(code->buf, buf, ARRAY_SIZE(buf));
  if (offset == 0 && (bno & 7) != RBP - RAX) {
    ;
  } else if (is_im8(offset)) {
    *p++ = IM8(offset);
  } else {
    PUT_CODE(p, IM32(offset));
    p += 4;
  }
  return p;
}

/* mirror of asm_movq_ixmm: movq %xmm, (mem), same 0f 7e opcode
 * asm_movq_xmmgp uses, addressing a memory operand instead of a gp
 * register */
static unsigned char *
asm_movq_xmmi(struct Inst *inst, struct Code *code)
{
  long offset;

  if (inst->opr[1].indirect.reg.no == RIP)
    return NULL;
  if (inst->opr[1].indirect.offset.expr->kind != EX_FIXNUM
      || !(offset = inst->opr[1].indirect.offset.expr->fixnum, is_im32(offset)))
    return NULL;
  {
    unsigned char sno   = inst->opr[0].regxmm - XMM0;
    unsigned char dno   = opr_regno(&inst->opr[1].indirect.reg);
    int           d     = dno & 7;
    int           s     = sno & 7;
    unsigned char op    = (offset == 0 && d != RBP - RAX) ? (unsigned char)0x00
                          : is_im8(offset)                ? (unsigned char)0x40
                                                          : (unsigned char)0x80;
    short         buf[] = {
        0x66,
        (unsigned char)0x48 | ((dno & 8) >> 3) | ((sno & 8) >> 1),
        0x0f,
        0x7e,
        op | d | (s << 3),
        d == RSP - RAX ? 0x24 : -1,
    };
    unsigned char *p = code->buf;
    p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    if (offset == 0 && d != RBP - RAX) {
      ;
    } else if (is_im8(offset)) {
      *p++ = IM8(offset);
    } else {
      PUT_CODE(p, IM32(offset));
      p += 4;
    }
    return p;
  }
}

static unsigned char *
asm_endbr64(struct Inst *inst, struct Code *code)
{
  UNUSED(inst);
  static const short buf[] = {
      0xf3,
      0x0f,
      0x1e,
      0xfa,
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static long
signed_immediate(long value, enum RegSize size)
{
  switch (size) {
    case REG8:
      return (int8_t)value;
    case REG16:
      return (int16_t)value;
    case REG32:
      return (int32_t)value;
    case REG64:
      return (int64_t)value;
  }
  assert(!"Must not reached");
  return value;
}

static unsigned char *
asm_lea_ir(struct Inst *inst, struct Code *code)
{
  assert(inst->opr[0].indirect.offset.expr != NULL);
  unsigned char *p = code->buf;
  if (inst->opr[0].indirect.reg.no != RIP) {
    if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM) {
      long         offset = inst->opr[0].indirect.offset.expr->fixnum;
      enum RegSize size   = inst->opr[1].reg.size;
      short        buf[]  = {
          MAKE_REX_INDIRECT(
              size,
              opr_regno(&inst->opr[1].reg),
              opr_regno(&inst->opr[0].indirect.reg),
              0x8c,
              0x00,
              offset
          ),
          MAKE_OFFSET(offset, inst->opr[0].indirect.reg.no != RBP - RAX),
      };
      p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      return p;
    }
  } else {
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM) {
      int pre = !inst->opr[1].reg.x ? 0x48 : 0x4c;
      int d   = inst->opr[1].reg.no;
      MAKE_CODE(inst, code, pre, 0x8d, 0x05 | (d << 3), IM32(0));
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_lea_iir(struct Inst *inst, struct Code *code)
{
  struct Expr *offset_expr = inst->opr[0].indirect_with_index.offset;
  struct Expr *scale_expr  = inst->opr[0].indirect_with_index.scale;
  if ((offset_expr == NULL || offset_expr->kind == EX_FIXNUM)
      && (scale_expr == NULL || scale_expr->kind == EX_FIXNUM)) {
    long offset = offset_expr != NULL ? offset_expr->fixnum : 0;
    long scale  = scale_expr != NULL ? scale_expr->fixnum : 1;
    if (is_im32(offset) && 1 <= scale && scale <= 8 && IS_POWER_OF_2(scale)) {
      int   breg  = opr_regno(&inst->opr[0].indirect_with_index.base_reg);
      int   ireg  = opr_regno(&inst->opr[0].indirect_with_index.index_reg);
      int   dreg  = opr_regno(&inst->opr[1].reg);
      int   noofs = offset == 0 && breg != RBP - RAX;
      int   pow   = most_significant_bit(scale);
      short buf[] = {
          0x48 | ((breg & 8) >> 3) | ((ireg & 8) >> 2) | ((dreg & 8) >> 1),
          0x8d,
          (noofs            ? 0x00
           : is_im8(offset) ? 0x40
                            : 0x80)
              | ((dreg & 7) << 3) | 0x04,
          (breg & 7) | ((ireg & 7) << 3) | (pow << 6),
          breg == RSP - RAX ? 0x24 : -1,
      };

      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      if (noofs) {
        ;
      } else if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else if (is_im32(offset)) {
        PUT_CODE(p, IM32(offset));
        p += 4;
      }
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_add_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[0].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x00 : 0x01
  );
  return p;
}

static unsigned char *
asm_add_imr(struct Inst *inst, struct Code *code)
{
  long value = inst->opr[0].immediate;
  if (is_im32(value)) {
    enum RegSize   size = inst->opr[1].reg.size;
    int            im8  = is_im8(value) || size == REG8;
    int            d    = opr_regno(&inst->opr[1].reg);
    unsigned char *p    = code->buf;
    if (d == RAX - RAX && (size == REG8 || !im8))
      p = put_rex0(p, size, 0, d, im8 ? 0x04 : 0x05);
    else
      p = put_rex1(p, size, 0xc0, d, size == REG8 ? 0x80 : im8 ? 0x83 : 0x81);

    if (im8) {
      *p++ = IM8(value);
    } else {
      switch (size) {
        case REG8:
          *p++ = IM8(value);
          break;
        case REG16:
          PUT_CODE(p, IM16(value));
          p += 2;
          break;
        case REG32:
        case REG64:
          PUT_CODE(p, IM32(value));
          p += 4;
          break;
      }
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_add_ir(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect.reg.no != RIP) {
    if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM) {
      long           offset = inst->opr[0].indirect.offset.expr->fixnum;
      enum RegSize   size   = inst->opr[1].reg.size;
      unsigned char *p      = code->buf;
      short          buf[]  = {
          MAKE_REX_INDIRECT(
              size,
              opr_regno(&inst->opr[1].reg),
              opr_regno(&inst->opr[0].indirect.reg),
              0x02,
              0x00,
              offset
          ),
          MAKE_OFFSET(offset, inst->opr[0].indirect.reg.no != RBP - RAX),
      };
      p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      return p;
    }
  } else {
    /* sym(%rip)/sym@gottpoff(%rip): symbolic rip-relative operand,
     * disp32 patched later once the target address (or got slot) is
     * known, same shape as asm_mov_ir's rip branch */
    if (inst->opr[0].indirect.offset.expr->kind != EX_FIXNUM) {
      int pre = !inst->opr[1].reg.x ? 0x48 : 0x4c;
      int d   = inst->opr[1].reg.no;
      MAKE_CODE(inst, code, pre, 0x03, 0x05 | (d << 3), IM32(0));
      return code->buf;
    }
  }
  return NULL;
}

static unsigned char *
asm_add_iir(struct Inst *inst, struct Code *code)
{
  assert(
      inst->opr[0].indirect_with_index.offset->kind == EX_FIXNUM
      && inst->opr[0].indirect_with_index.offset->fixnum == 0
  ); /* TODO */
  unsigned char scale      = 0;
  struct Expr  *scale_expr = inst->opr[0].indirect_with_index.scale;
  if (scale_expr != NULL) {
    assert(scale_expr->kind == EX_FIXNUM);
    int s = scale_expr->fixnum;
    if (s <= 0 || s > 8 || !IS_POWER_OF_2(s))
      return NULL;
    scale = most_significant_bit(s);
  }

  unsigned char bno   = inst->opr[0].indirect_with_index.base_reg.no;
  unsigned char ino   = inst->opr[0].indirect_with_index.index_reg.no;
  unsigned char dno   = inst->opr[1].reg.no;
  short         buf[] = {
      (unsigned char)0x48 | (inst->opr[0].indirect_with_index.base_reg.x)
          | (inst->opr[0].indirect_with_index.index_reg.x << 1) | (inst->opr[1].reg.x << 2),
      0x03,
      0x04 | (dno << 3),
      bno | (ino << 3) | (scale << 6),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_addq_imi(struct Inst *inst, struct Code *code)
{
  if (inst->opr[1].indirect.offset.expr->kind == EX_FIXNUM) {
    long value = inst->opr[0].immediate;
    if (is_im32(value)) {
      long          offset = inst->opr[1].indirect.offset.expr->fixnum;
      unsigned char sno    = 0;
      unsigned char dno    = opr_regno(&inst->opr[1].indirect.reg);
      unsigned char op     = (offset == 0 && (dno & 7) != RBP - RAX) ? 0x00
                             : is_im8(offset)                        ? (unsigned char)0x40
                                                                     : (unsigned char)0x80;
      short         buf[]  = {
          (unsigned char)0x48 | ((dno & 8) >> 3) | ((sno & 8) >> 1),
          (is_im8(value) ? 0x83 : 0x81),
          op | (dno & 7) | ((sno & 7) << 3),
          (dno & 7) == RSP - RAX ? 0x24 : -1,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      if (offset == 0 && (dno & 7) != RBP - RAX) {
        ;
      } else if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else if (is_im32(offset)) {
        PUT_CODE(p, IM32(offset));
        p += 4;
      }

      if (is_im8(value)) {
        *p++ = IM8(value);
      } else {
        PUT_CODE(p, IM32(value));
        p += 4;
      }
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_sub_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[0].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x28 : 0x29
  );
  return p;
}

static unsigned char *
asm_sub_imr(struct Inst *inst, struct Code *code)
{
  long value = inst->opr[0].immediate;
  if (is_im32(value)) {
    enum RegSize   size = inst->opr[1].reg.size;
    int            im8  = is_im8(value) || size == REG8;
    int            d    = opr_regno(&inst->opr[1].reg);
    unsigned char *p    = code->buf;
    if (d == RAX - RAX && (size == REG8 || !im8))
      p = put_rex0(p, size, 0, d, im8 ? 0x2c : 0x2d);
    else
      p = put_rex1(p, size, 0xe8, d, size == REG8 ? 0x80 : im8 ? 0x83 : 0x81);

    if (im8) {
      *p++ = IM8(value);
    } else {
      switch (size) {
        case REG8:
          *p++ = IM8(value);
          break;
        case REG16:
          PUT_CODE(p, IM16(value));
          p += 2;
          break;
        case REG32:
        case REG64:
          PUT_CODE(p, IM32(value));
          p += 4;
          break;
      }
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_sub_ir(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect.offset.expr->kind == EX_FIXNUM && inst->opr[0].indirect.reg.no != RIP) {
    long           offset = inst->opr[0].indirect.offset.expr->fixnum;
    enum RegSize   size   = inst->opr[1].reg.size;
    unsigned char *p      = code->buf;
    short          buf[]  = {
        MAKE_REX_INDIRECT(
            size,
            opr_regno(&inst->opr[1].reg),
            opr_regno(&inst->opr[0].indirect.reg),
            0x2a,
            0x00,
            offset
        ),
        MAKE_OFFSET(offset, inst->opr[0].indirect.reg.no != RBP - RAX),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    return p;
  }
  return NULL;
}

static unsigned char *
asm_sub_iir(struct Inst *inst, struct Code *code)
{
  assert(
      inst->opr[0].indirect_with_index.offset->kind == EX_FIXNUM
      && inst->opr[0].indirect_with_index.offset->fixnum == 0
  ); /* TODO */
  unsigned char scale      = 0;
  struct Expr  *scale_expr = inst->opr[0].indirect_with_index.scale;
  if (scale_expr != NULL) {
    assert(scale_expr->kind == EX_FIXNUM);
    int s = scale_expr->fixnum;
    if (s <= 0 || s > 8 || !IS_POWER_OF_2(s))
      return NULL;
    scale = most_significant_bit(s);
  }

  unsigned char bno   = inst->opr[0].indirect_with_index.base_reg.no;
  unsigned char ino   = inst->opr[0].indirect_with_index.index_reg.no;
  unsigned char dno   = inst->opr[1].reg.no;
  short         buf[] = {
      (unsigned char)0x48 | (inst->opr[0].indirect_with_index.base_reg.x)
          | (inst->opr[0].indirect_with_index.index_reg.x << 1) | (inst->opr[1].reg.x << 2),
      0x2b,
      0x04 | (dno << 3),
      bno | (ino << 3) | (scale << 6),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_subq_imi(struct Inst *inst, struct Code *code)
{
  if (inst->opr[1].indirect.offset.expr->kind == EX_FIXNUM) {
    long value = inst->opr[0].immediate;
    if (is_im32(value)) {
      long          offset = inst->opr[1].indirect.offset.expr->fixnum;
      unsigned char sno    = 0;
      unsigned char dno    = opr_regno(&inst->opr[1].indirect.reg);
      unsigned char op     = (offset == 0 && (dno & 7) != RBP - RAX) ? 0x28
                             : is_im8(offset)                        ? (unsigned char)0x40
                                                                     : (unsigned char)0x80;
      short         buf[]  = {
          (unsigned char)0x48 | ((dno & 8) >> 3) | ((sno & 8) >> 1),
          (is_im8(value) ? 0x83 : 0x81),
          op | (dno & 7) | ((sno & 7) << 3),
          (dno & 7) == RSP - RAX ? 0x24 : -1,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      if (offset == 0 && (dno & 7) != RBP - RAX) {
        ;
      } else if (is_im8(offset)) {
        *p++ = IM8(offset);
      } else if (is_im32(offset)) {
        PUT_CODE(p, IM32(offset));
        p += 4;
      }

      if (is_im8(value)) {
        *p++ = IM8(value);
      } else {
        PUT_CODE(p, IM32(value));
        p += 4;
      }
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_mul_r(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[0].reg.size;
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(size, 0, opr_regno(&inst->opr[0].reg), 0xf6 | (size == REG8 ? 0 : 1)),
      0xe0 | inst->opr[0].reg.no,
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_div_r(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[0].reg.size;
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(size, 0, opr_regno(&inst->opr[0].reg), 0xf6 | (size == REG8 ? 0 : 1)),
      0xf0 | inst->opr[0].reg.no,
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_idiv_r(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[0].reg.size;
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(size, 0, opr_regno(&inst->opr[0].reg), 0xf6 | (size == REG8 ? 0 : 1)),
      0xf8 | inst->opr[0].reg.no,
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_neg_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p = code->buf;
  p                = put_rex1(p, inst->opr[0].reg.size, 0xd8, opr_regno(&inst->opr[0].reg), 0xf7);
  return p;
}

static unsigned char *
asm_not_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p = code->buf;
  p                = put_rex1(p, inst->opr[0].reg.size, 0xd0, opr_regno(&inst->opr[0].reg), 0xf7);
  return p;
}

static unsigned char *
asm_inc_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p   = code->buf;
  struct Reg    *reg = &inst->opr[0].reg;
  p = put_rex1(p, reg->size, 0xc0, opr_regno(reg), reg->size == REG8 ? 0xfe : 0xff);
  return p;
}

static unsigned char *
asm_incbwlq_i(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect.reg.no != RIP) {
    enum RegSize   size   = inst->op + (REG8 - INCB);
    long           offset = inst->opr[0].indirect.offset.expr->fixnum;
    unsigned char *p      = code->buf;
    short          buf[]  = {
        MAKE_REX_INDIRECT(size, 0, opr_regno(&inst->opr[0].indirect.reg), 0xfe, 0x00, offset),
        MAKE_OFFSET(offset, inst->opr[0].indirect.reg.no != RBP - RAX),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    return p;
  }
  return NULL;
}

static unsigned char *
asm_dec_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p   = code->buf;
  struct Reg    *reg = &inst->opr[0].reg;
  p = put_rex1(p, reg->size, 0xc8, opr_regno(reg), reg->size == REG8 ? 0xfe : 0xff);
  return p;
}

static unsigned char *
asm_decbwlq_i(struct Inst *inst, struct Code *code)
{
  if (inst->opr[0].indirect.reg.no != RIP) {
    enum RegSize   size   = inst->op + (REG8 - DECB);
    long           offset = inst->opr[0].indirect.offset.expr->fixnum;
    unsigned char *p      = code->buf;
    short          buf[]  = {
        MAKE_REX_INDIRECT(size, 1, opr_regno(&inst->opr[0].indirect.reg), 0xfe, 0x08, offset),
        MAKE_OFFSET(offset, inst->opr[0].indirect.reg.no != RBP - RAX),
    };
    p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    return p;
  }
  return NULL;
}

static unsigned char *
asm_and_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x20 : 0x21
  );
  return p;
}

static unsigned char *
asm_and_imr(struct Inst *inst, struct Code *code)
{
  long           value = inst->opr[0].immediate;
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  if (is_im8(value) && (size != REG8 || opr_regno(&inst->opr[1].reg) != AL - AL)) {
    p    = put_rex1(p, size, 0xe0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x83);
    *p++ = IM8(value);
    return p;
  } else if (size <= REG32 || is_im32(value)) {
    if (opr_regno(&inst->opr[1].reg) == RAX - RAX) {
      p = put_rex0(p, size, 0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x24 : 0x25);
    } else {
      p = put_rex1(p, size, 0xe0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x81);
    }

    switch (size) {
      case REG8:
        PUT_CODE(p, IM8(value));
        p += 1;
        break;
      case REG16:
        PUT_CODE(p, IM16(value));
        p += 2;
        break;
      case REG32:
      case REG64:
        PUT_CODE(p, IM32(value));
        p += 4;
        break;
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_or_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x08 : 0x09
  );
  return p;
}

static unsigned char *
asm_or_imr(struct Inst *inst, struct Code *code)
{
  long           value = inst->opr[0].immediate;
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  if (is_im8(value) && (size != REG8 || opr_regno(&inst->opr[1].reg) != AL - AL)) {
    p    = put_rex1(p, size, 0xc8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x83);
    *p++ = IM8(value);
    return p;
  } else if (size <= REG32 || is_im32(value)) {
    if (opr_regno(&inst->opr[1].reg) == RAX - RAX) {
      p = put_rex0(p, size, 0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x0c : 0x0d);
    } else {
      p = put_rex1(p, size, 0xc8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x81);
    }
    switch (size) {
      case REG8:
        PUT_CODE(p, IM8(value));
        p += 1;
        break;
      case REG16:
        PUT_CODE(p, IM16(value));
        p += 2;
        break;
      case REG32:
      case REG64:
        PUT_CODE(p, IM32(value));
        p += 4;
        break;
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_xor_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x30 : 0x31
  );
  return p;
}

static unsigned char *
asm_xor_imr(struct Inst *inst, struct Code *code)
{
  long           value = inst->opr[0].immediate;
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  if (is_im8(value) && (size != REG8 || opr_regno(&inst->opr[1].reg) != AL - AL)) {
    p    = put_rex1(p, size, 0xf0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x83);
    *p++ = IM8(value);
    return p;
  } else if (size <= REG32 || is_im32(value)) {
    if (opr_regno(&inst->opr[1].reg) == RAX - RAX) {
      p = put_rex0(p, size, 0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x34 : 0x35);
    } else {
      p = put_rex1(p, size, 0xf0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0x80 : 0x81);
    }
    switch (size) {
      case REG8:
        PUT_CODE(p, IM8(value));
        p += 1;
        break;
      case REG16:
        PUT_CODE(p, IM16(value));
        p += 2;
        break;
      case REG32:
      case REG64:
        PUT_CODE(p, IM32(value));
        p += 4;
        break;
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_shl_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p = put_rex1(p, size, 0xe0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd2 : 0xd3);
  return p;
}

static unsigned char *
asm_shl_imr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  uint64_t       shift = inst->opr[0].immediate;
  if (shift >= 256)
    return NULL;
  if (shift == 1) {
    p = put_rex1(p, size, 0xe0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd0 : 0xd1);
  } else {
    p    = put_rex1(p, size, 0xe0, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xc0 : 0xc1);
    *p++ = IM8(shift);
  }
  return p;
}

static unsigned char *
asm_shr_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p = put_rex1(p, size, 0xe8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd2 : 0xd3);
  return p;
}

static unsigned char *
asm_shr_imr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  uint64_t       shift = inst->opr[0].immediate;
  if (shift >= 256)
    return NULL;
  if (shift == 1) {
    p = put_rex1(p, size, 0xe8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd0 : 0xd1);
  } else {
    p    = put_rex1(p, size, 0xe8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xc0 : 0xc1);
    *p++ = IM8(shift);
  }
  return p;
}

static unsigned char *
asm_sar_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[1].reg.size;
  unsigned char *p    = code->buf;
  p = put_rex1(p, size, 0xf8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd2 : 0xd3);
  return p;
}

static unsigned char *
asm_sar_imr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[1].reg.size;
  unsigned char *p     = code->buf;
  uint64_t       shift = inst->opr[0].immediate;
  if (shift >= 256)
    return NULL;
  if (shift == 1) {
    p = put_rex1(p, size, 0xf8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xd0 : 0xd1);
  } else {
    p    = put_rex1(p, size, 0xf8, opr_regno(&inst->opr[1].reg), size == REG8 ? 0xc0 : 0xc1);
    *p++ = IM8(shift);
  }
  return p;
}

static unsigned char *
asm_cmp_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[0].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x38 : 0x39
  );
  return p;
}

static unsigned char *
asm_cmp_imr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[1].reg.size;
  long         value = signed_immediate(inst->opr[0].immediate, size);
  if (is_im32(value) || size <= REG32) {
    int            im8 = is_im8(value);
    int            d   = opr_regno(&inst->opr[1].reg);
    unsigned char *p   = code->buf;
    if (d == RAX - RAX && (size == REG8 || !im8))
      p = put_rex0(p, size, 0, d, size == REG8 ? 0x3c : 0x3d);
    else
      p = put_rex1(p, size, 0xf8, d, 0x80 | (size == REG8 ? 0 : im8 ? 3 : 1));

    if (im8) {
      *p++ = IM8(value);
    } else {
      switch (size) {
        case REG8:
          *p++ = IM8(value);
          break;
        case REG16:
          PUT_CODE(p, IM16(value));
          p += 2;
          break;
        case REG32:
        case REG64:
          PUT_CODE(p, IM32(value));
          p += 4;
          break;
      }
    }
    return p;
  }
  return NULL;
}

static unsigned char *
asm_test_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[0].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x84 : 0x85
  );
  return p;
}

static unsigned char *
asm_cwtl(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x98);
  return code->buf;
}

static unsigned char *
asm_cltd(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x99);
  return code->buf;
}

static unsigned char *
asm_cqto(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x48, 0x99);
  return code->buf;
}

static unsigned char *
asm_bsr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, dno, sno, 0x0f),
      0xbd,
      0xc0 | (inst->opr[1].reg.no << 3) | inst->opr[0].reg.no,
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_tzcnt(struct Inst *inst, struct Code *code)
{
  enum RegSize  size   = inst->opr[0].reg.size;
  unsigned char prefix = 0xf3;
  int           sno    = opr_regno(&inst->opr[0].reg);
  int           dno    = opr_regno(&inst->opr[1].reg);
  short         buf[]  = {
      (size) == REG16 ? 0x66 : -1,
      prefix,
      sno >= 8 || dno >= 8 || size == REG64
          ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) | (size != REG64 ? 0 : 8)
          : -1,
      0x0f,
      inst->op == LZCNT ? 0xbd : 0xbc,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_popcnt(struct Inst *inst, struct Code *code)
{
  enum RegSize  size   = inst->opr[0].reg.size;
  unsigned char prefix = 0xf3;
  int           sno    = opr_regno(&inst->opr[0].reg);
  int           dno    = opr_regno(&inst->opr[1].reg);
  short         buf[]  = {
      (size) == REG16 ? 0x66 : -1,
      prefix,
      sno >= 8 || dno >= 8 || size == REG64
          ? (unsigned char)0x40 | ((sno & 8) >> 3) | ((dno & 8) >> 1) | (size != REG64 ? 0 : 8)
          : -1,
      0x0f,
      0xb8,
      (unsigned char)0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_set_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(REG8, 0, opr_regno(&inst->opr[0].reg), 0x0f),
      0x90 | (inst->op - SETO),
      0xc0 | inst->opr[0].reg.no,
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_push_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(REG32, 0, opr_regno(&inst->opr[0].reg), 0x50 | inst->opr[0].reg.no),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_push_im(struct Inst *inst, struct Code *code)
{
  unsigned char *p     = code->buf;
  long           value = inst->opr[0].immediate;
  if (is_im8(value)) {
    *p++ = 0x6a;
    *p++ = IM8(value);
    return p;
  } else if (is_im32(value)) {
    *p++ = 0x68;
    PUT_CODE(p, IM32(value));
    p += 4;
    return p;
  }
  return NULL;
}

static unsigned char *
asm_pop_r(struct Inst *inst, struct Code *code)
{
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(REG32, 0, opr_regno(&inst->opr[0].reg), 0x58 | inst->opr[0].reg.no),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_jmp_d(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0xeb, IM8(0)); /* short jmp in default */
  return code->buf;
}

static unsigned char *
asm_jmp_der(struct Inst *inst, struct Code *code)
{
  int   s     = inst->opr[0].reg.no;
  short buf[] = {
      inst->opr[0].reg.x ? 0x41 : -1,
      0xff,
      0xe0 | s,
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

static unsigned char *
asm_jmp_dei(struct Inst *inst, struct Code *code)
{
  struct Expr *offset_expr = inst->opr[0].indirect.offset.expr;
  if ((offset_expr == NULL || offset_expr->kind == EX_FIXNUM)) {
    long offset = offset_expr != NULL ? offset_expr->fixnum : 0;
    if (is_im32(offset)) {
      short offset_bit = offset == 0 ? 0x20 : is_im8(offset) ? 0x60 : 0xa0;
      short b          = inst->opr[0].indirect.reg.no;
      short buf[]      = {
          inst->opr[0].indirect.reg.x ? 0x41 : -1,
          0xff,
          offset_bit | b,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      if (offset != 0) {
        if (is_im8(offset)) {
          *p++ = IM8(offset);
        } else {
          PUT_CODE(p, IM32(offset));
          p += 4;
        }
      }
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_jmp_deii(struct Inst *inst, struct Code *code)
{
  struct Expr *offset_expr = inst->opr[0].indirect_with_index.offset;
  struct Expr *scale_expr  = inst->opr[0].indirect_with_index.scale;
  if ((offset_expr == NULL || offset_expr->kind == EX_FIXNUM)
      && (scale_expr == NULL || scale_expr->kind == EX_FIXNUM)) {
    long offset = offset_expr != NULL ? offset_expr->fixnum : 0;
    long scale  = scale_expr != NULL ? scale_expr->fixnum : 1;
    if (is_im32(offset) && 1 <= scale && scale <= 8 && IS_POWER_OF_2(scale)) {
      short b          = inst->opr[0].indirect_with_index.base_reg.no;
      short scale_bit  = most_significant_bit(scale);
      short i          = inst->opr[0].indirect_with_index.index_reg.no;
      short prefix     = inst->opr[0].indirect_with_index.base_reg.x
                         | (inst->opr[0].indirect_with_index.index_reg.x << 1);
      short offset_bit = offset == 0 && b != RBP - RAX ? 0x20 : is_im8(offset) ? 0x60 : 0xa0;
      short buf[]      = {
          prefix != 0 ? 0x40 | prefix : -1,
          0xff,
          offset_bit | 0x04,
          (scale_bit << 6) | (i << 3) | b,
      };
      unsigned char *p = code->buf;
      p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
      if (offset != 0 || b == RBP - RAX) {
        if (is_im8(offset)) {
          *p++ = IM8(offset);
        } else {
          PUT_CODE(p, IM32(offset));
          p += 4;
        }
      }
      return p;
    }
  }
  return NULL;
}

static unsigned char *
asm_jxx_d(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x70 + (inst->op - JO), IM8(0)); /* short jmp in default */
  return code->buf;
}

static unsigned char *
asm_call_d(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0xe8, IM32(0));
  return code->buf;
}

static unsigned char *
asm_call_der(struct Inst *inst, struct Code *code)
{
  int s = inst->opr[0].reg.no;
  if (!inst->opr[0].reg.x) {
    MAKE_CODE(inst, code, 0xff, 0xd0 + s);
  } else {
    MAKE_CODE(inst, code, 0x41, 0xff, 0xd0 + s);
  }
  return code->buf;
}

static unsigned char *
asm_ret(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0xc3);
  return code->buf;
}

static unsigned char *
asm_int_im(struct Inst *inst, struct Code *code)
{
  long value = inst->opr[0].immediate;
  MAKE_CODE(inst, code, 0xcd, IM8(value));
  return code->buf;
}

static unsigned char *
asm_syscall(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x0f, 0x05);
  return code->buf;
}

/*// //////////////////////////////////////////// */
/* new instruction encoders */

/* no-operand fixed-encoding instructions */
static unsigned char *
asm_simple(struct Inst *inst, struct Code *code)
{
  switch (inst->op) {
    case NOP:
      MAKE_CODE(inst, code, 0x90);
      return code->buf;
    case PUSHFQ:
      MAKE_CODE(inst, code, 0x9c);
      return code->buf;
    case POPFQ:
      MAKE_CODE(inst, code, 0x9d);
      return code->buf;
    case PUSHF:
      MAKE_CODE(inst, code, 0x66, 0x9c);
      return code->buf;
    case POPF:
      MAKE_CODE(inst, code, 0x66, 0x9d);
      return code->buf;
    case CPUID:
      MAKE_CODE(inst, code, 0x0f, 0xa2);
      return code->buf;
    case RDTSC:
      MAKE_CODE(inst, code, 0x0f, 0x31);
      return code->buf;
    case UD2:
      MAKE_CODE(inst, code, 0x0f, 0x0b);
      return code->buf;
    case PAUSE:
      MAKE_CODE(inst, code, 0xf3, 0x90);
      return code->buf;
    case HLT:
      MAKE_CODE(inst, code, 0xf4);
      return code->buf;
    case CMC:
      MAKE_CODE(inst, code, 0xf5);
      return code->buf;
    case CLC:
      MAKE_CODE(inst, code, 0xf8);
      return code->buf;
    case STC:
      MAKE_CODE(inst, code, 0xf9);
      return code->buf;
    case CLI:
      MAKE_CODE(inst, code, 0xfa);
      return code->buf;
    case STI:
      MAKE_CODE(inst, code, 0xfb);
      return code->buf;
    case CLD:
      MAKE_CODE(inst, code, 0xfc);
      return code->buf;
    case STD:
      MAKE_CODE(inst, code, 0xfd);
      return code->buf;
    case CLTS:
      MAKE_CODE(inst, code, 0x0f, 0x06);
      return code->buf;
    case WBINVD:
      MAKE_CODE(inst, code, 0x0f, 0x09);
      return code->buf;
    case MFENCE:
      MAKE_CODE(inst, code, 0x0f, 0xae, 0xf0);
      return code->buf;
    case LFENCE:
      MAKE_CODE(inst, code, 0x0f, 0xae, 0xe8);
      return code->buf;
    case SFENCE:
      MAKE_CODE(inst, code, 0x0f, 0xae, 0xf8);
      return code->buf;
    default:
      return NULL;
  }
}

/* one-operand imul: f7 /5 */
static unsigned char *
asm_imul_r(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[0].reg.size;
  unsigned char *p     = code->buf;
  short          buf[] = {
      MAKE_REX0(size, 0, opr_regno(&inst->opr[0].reg), 0xf6 | (size == REG8 ? 0 : 1)),
      0xe8 | inst->opr[0].reg.no,
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* two-operand imul: 0f af /r, where reg is the destination (unlike
 * mov/add, where it is the source). MAKE_REX0 wants the reg-field
 * operand in the middle argument, so dno goes first, as in asm_bsf
 * below */
static unsigned char *
asm_imul_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size  = inst->opr[0].reg.size;
  unsigned char *p     = code->buf;
  int            sno   = opr_regno(&inst->opr[0].reg);
  int            dno   = opr_regno(&inst->opr[1].reg);
  short          buf[] = {
      MAKE_REX0(size, dno, sno, 0x0f),
      0xaf,
      0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  p = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* imul $imm, %reg: 6b/69 /r ib/id (reg used as both src and dst) */
static unsigned char *
asm_imul_irr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[1].reg.size;
  int          dno   = opr_regno(&inst->opr[1].reg);
  long         value = inst->opr[0].immediate;
  int          im8   = is_im8(value);
  short        buf[] = {
      MAKE_REX0(size, dno, dno, im8 ? 0x6b : 0x69),
      0xc0 | ((dno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  if (im8) {
    *p++ = IM8(value);
  } else {
    PUT_CODE(p, IM32(value));
    p += 4;
  }
  return p;
}

static unsigned char *
asm_leave(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0xc9);
  return code->buf;
}

static unsigned char *
asm_enter(struct Inst *inst, struct Code *code)
{
  long size  = inst->opr[0].immediate;
  long depth = inst->opr[1].immediate;
  MAKE_CODE(inst, code, 0xc8, IM16(size), IM8(depth));
  return code->buf;
}

/* xchg: 86/87 /r */
static unsigned char *
asm_xchg_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize   size = inst->opr[0].reg.size;
  unsigned char *p    = code->buf;
  p                   = put_rex2(
      p,
      size,
      opr_regno(&inst->opr[0].reg),
      opr_regno(&inst->opr[1].reg),
      size == REG8 ? 0x86 : 0x87
  );
  return p;
}

/* xchg reg, (mem): a memory operand always locks implicitly, needed
 * for the atomic runtime's __atomic_exchange_N */
static unsigned char *
asm_xchg_ri(struct Inst *inst, struct Code *code)
{
  return assemble_reg_to_mem(inst, code, 0, 0x86, 0x87);
}

/* bsf: 0f bc /r */
static unsigned char *
asm_bsf(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, dno, sno, 0x0f),
      0xbc,
      0xc0 | ((dno & 7) << 3) | (sno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* bswap: 0f c8+r (32/64 only) */
static unsigned char *
asm_bswap(struct Inst *inst, struct Code *code)
{
  enum RegSize size = inst->opr[0].reg.size;
  int          dno  = opr_regno(&inst->opr[0].reg);
  if (size != REG32 && size != REG64)
    return NULL;
  short buf[] = {
      MAKE_REX0(size, 0, dno, 0x0f),
      0xc8 | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* xadd: 0f c0(8bit)/c1(16+32+64) /r */
static unsigned char *
asm_xadd_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, sno, dno, 0x0f),
      size == REG8 ? 0xc0 : 0xc1,
      0xc0 | ((sno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* cmpxchg: 0f b0(8bit)/b1(16+32+64) /r */
static unsigned char *
asm_cmpxchg_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, sno, dno, 0x0f),
      size == REG8 ? 0xb0 : 0xb1,
      0xc0 | ((sno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* xadd/cmpxchg reg, (mem): 0f escape byte ahead of the real (r/m,r)
 * opcode pair, otherwise identical in shape to mov_ri. needed for the
 * atomic runtime's __atomic_fetch_add_N/__atomic_compare_exchange_N,
 * xadd also for __atomic_fetch_sub_N (negate then xadd) */
static unsigned char *
asm_xadd_ri(struct Inst *inst, struct Code *code)
{
  return assemble_reg_to_mem(inst, code, 1, 0xc0, 0xc1);
}

static unsigned char *
asm_cmpxchg_ri(struct Inst *inst, struct Code *code)
{
  return assemble_reg_to_mem(inst, code, 1, 0xb0, 0xb1);
}

/* bt family: register form 0f [a3|ab|b3|bb] /r,
 * immediate form 0f ba /[4|5|6|7] ib */
static unsigned char *
asm_bt_family(struct Inst *inst, struct Code *code)
{
  int rr_opcodes[] = {
      [BT_RR - BT_RR]  = 0xa3,
      [BTS_RR - BT_RR] = 0xab,
      [BTR_RR - BT_RR] = 0xb3,
      [BTC_RR - BT_RR] = 0xbb
  };
  int imm_digits[] = {
      [BT_RR - BT_RR] = 4, [BTS_RR - BT_RR] = 5, [BTR_RR - BT_RR] = 6, [BTC_RR - BT_RR] = 7
  };
  int idx = inst->op - BT_RR;

  if (inst->opr[0].type == IMMEDIATE) {
    enum RegSize size  = inst->opr[1].reg.size;
    int          dno   = opr_regno(&inst->opr[1].reg);
    long         value = inst->opr[0].immediate;
    short        buf[] = {
        MAKE_REX0(size, imm_digits[idx], dno, 0x0f),
        0xba,
        0xc0 | (imm_digits[idx] << 3) | (dno & 7),
        value & 0xff,
    };
    unsigned char *p = code->buf;
    p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
    return p;
  }

  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, sno, dno, 0x0f),
      rr_opcodes[idx],
      0xc0 | ((sno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* cmovcc: 0f 40+cc /r */
static unsigned char *
asm_cmov_rr(struct Inst *inst, struct Code *code)
{
  enum RegSize size  = inst->opr[0].reg.size;
  int          sno   = opr_regno(&inst->opr[0].reg);
  int          dno   = opr_regno(&inst->opr[1].reg);
  short        buf[] = {
      MAKE_REX0(size, sno, dno, 0x0f),
      0x40 + (inst->op - CMOVO),
      0xc0 | ((sno & 7) << 3) | (dno & 7),
  };
  unsigned char *p = code->buf;
  p                = put_code_filtered(p, buf, ARRAY_SIZE(buf));
  return p;
}

/* string operations: fixed encoding by size */
static unsigned char *
asm_string(struct Inst *inst, struct Code *code)
{
  /* base opcodes: movs=0xa4, stos=0xaa, lods=0xac, scas=0xae, cmps=0xa6
 * the byte form has opcode x, word/dword/qword form has opcode X+1
 * the table below stores the byte-form opcode for each group */
  static const unsigned char base[] = {
      [MOVSB - MOVSB] = 0xa4,
      [STOSB - MOVSB] = 0xaa,
      [LODSB - MOVSB] = 0xac,
      [SCASB - MOVSB] = 0xae,
      [CMPSB - MOVSB] = 0xa6,
  };
  int group_idx = (inst->op - MOVSB) / 4;
  int size_idx  = (inst->op - MOVSB) % 4; /* 0=byte, 1=word, 2=long, 3=quad */

  if (group_idx > 4)
    return NULL;

  unsigned char opcode = base[group_idx * 4];

  switch (size_idx) {
    case 0: /* byte */
      MAKE_CODE(inst, code, opcode);
      break;
    case 1: /* word */
      MAKE_CODE(inst, code, 0x66, opcode + 1);
      break;
    case 2: /* long */
      MAKE_CODE(inst, code, opcode + 1);
      break;
    case 3: /* quad */
      MAKE_CODE(inst, code, 0x48, opcode + 1);
      break;
  }
  return code->buf;
}

/* gas intel-syntax conversion aliases */
static unsigned char *
asm_cbw(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x66, 0x98);
  return code->buf;
}

static unsigned char *
asm_cdqe(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x48, 0x98);
  return code->buf;
}

static unsigned char *
asm_cwd(struct Inst *inst, struct Code *code)
{
  MAKE_CODE(inst, code, 0x66, 0x99);
  return code->buf;
}

/*// //////////////////////////////////////////// */

typedef unsigned char *(*AsmInstFunc)(struct Inst *inst, struct Code *code);

static const AsmInstFunc table[] = {
    [NOOP]     = asm_noop,
    [MOV_RR]   = asm_mov_rr,
    [MOV_IMR]  = asm_mov_imr,
    [MOV_IR]   = asm_mov_ir,
    [MOV_RI]   = asm_mov_ri,
    [MOV_IIR]  = asm_mov_iir,
    [MOV_DR]   = asm_mov_dr,
    [MOV_RD]   = asm_mov_rd,
    [MOV_SR]   = asm_mov_sr,
    [MOVB_IMI] = asm_movbwlq_imi,
    [MOVW_IMI] = asm_movbwlq_imi,
    [MOVL_IMI] = asm_movbwlq_imi,
    [MOVQ_IMI] = asm_movbwlq_imi,
    [MOVB_IMD] = asm_movbwlq_imd,
    [MOVW_IMD] = asm_movbwlq_imd,
    [MOVL_IMD] = asm_movbwlq_imd,
    [MOVQ_IMD] = asm_movbwlq_imd,
    [MOVSX]    = asm_movszx_rr,
    [MOVZX]    = asm_movszx_rr,
    [LEA_IR]   = asm_lea_ir,
    [LEA_IIR]  = asm_lea_iir,
    [ADD_RR]   = asm_add_rr,
    [ADD_IMR]  = asm_add_imr,
    [ADD_IR]   = asm_add_ir,
    [ADD_IIR]  = asm_add_iir,
    [ADDQ]     = asm_addq_imi,
    [SUB_RR]   = asm_sub_rr,
    [SUB_IMR]  = asm_sub_imr,
    [SUB_IR]   = asm_sub_ir,
    [SUB_IIR]  = asm_sub_iir,
    [SUBQ]     = asm_subq_imi,
    [MUL]      = asm_mul_r,
    [DIV]      = asm_div_r,
    [IDIV]     = asm_idiv_r,
    [NEG]      = asm_neg_r,
    [NOT]      = asm_not_r,
    [INC]      = asm_inc_r,
    [INCB]     = asm_incbwlq_i,
    [INCW]     = asm_incbwlq_i,
    [INCL]     = asm_incbwlq_i,
    [INCQ]     = asm_incbwlq_i,
    [DEC]      = asm_dec_r,
    [DECB]     = asm_decbwlq_i,
    [DECW]     = asm_decbwlq_i,
    [DECL]     = asm_decbwlq_i,
    [DECQ]     = asm_decbwlq_i,
    [AND_RR]   = asm_and_rr,
    [AND_IMR]  = asm_and_imr,
    [OR_RR]    = asm_or_rr,
    [OR_IMR]   = asm_or_imr,
    [XOR_RR]   = asm_xor_rr,
    [XOR_IMR]  = asm_xor_imr,
    [SHL_RR]   = asm_shl_rr,
    [SHL_IMR]  = asm_shl_imr,
    [SHR_RR]   = asm_shr_rr,
    [SHR_IMR]  = asm_shr_imr,
    [SAR_RR]   = asm_sar_rr,
    [SAR_IMR]  = asm_sar_imr,
    [CMP_RR]   = asm_cmp_rr,
    [CMP_IMR]  = asm_cmp_imr,
    [TEST]     = asm_test_rr,
    [CWTL]     = asm_cwtl,
    [CLTD]     = asm_cltd,
    [CQTO]     = asm_cqto,
    [BSR]      = asm_bsr,
    [LZCNT]    = asm_tzcnt,
    [TZCNT]    = asm_tzcnt,
    [POPCNT]   = asm_popcnt,
    [SETO]     = asm_set_r,
    [SETNO]    = asm_set_r,
    [SETB]     = asm_set_r,
    [SETAE]    = asm_set_r,
    [SETE]     = asm_set_r,
    [SETNE]    = asm_set_r,
    [SETBE]    = asm_set_r,
    [SETA]     = asm_set_r,
    [SETS]     = asm_set_r,
    [SETNS]    = asm_set_r,
    [SETP]     = asm_set_r,
    [SETNP]    = asm_set_r,
    [SETL]     = asm_set_r,
    [SETGE]    = asm_set_r,
    [SETLE]    = asm_set_r,
    [SETG]     = asm_set_r,
    [JMP_D]    = asm_jmp_d,
    [JMP_DER]  = asm_jmp_der,
    [JMP_DEI]  = asm_jmp_dei,
    [JMP_DEII] = asm_jmp_deii,
    [JO]       = asm_jxx_d,
    [JNO]      = asm_jxx_d,
    [JB]       = asm_jxx_d,
    [JAE]      = asm_jxx_d,
    [JE]       = asm_jxx_d,
    [JNE]      = asm_jxx_d,
    [JBE]      = asm_jxx_d,
    [JA]       = asm_jxx_d,
    [JS]       = asm_jxx_d,
    [JNS]      = asm_jxx_d,
    [JP]       = asm_jxx_d,
    [JNP]      = asm_jxx_d,
    [JL]       = asm_jxx_d,
    [JGE]      = asm_jxx_d,
    [JLE]      = asm_jxx_d,
    [JG]       = asm_jxx_d,
    [CALL_D]   = asm_call_d,
    [CALL_DER] = asm_call_der,
    [RET]      = asm_ret,
    [PUSH_R]   = asm_push_r,
    [PUSH_IM]  = asm_push_im,
    [POP]      = asm_pop_r,
    [INT]      = asm_int_im,
    [SYSCALL]  = asm_syscall,

    [MOVSD_XX]     = asm_movsd_xx,
    [MOVSD_IX]     = asm_movsd_ix,
    [MOVSD_XI]     = asm_movsd_xi,
    [MOVSS_XX]     = asm_movss_xx,
    [MOVSS_IX]     = asm_movss_ix,
    [MOVSS_XI]     = asm_movss_xi,
    [ADDSD]        = asm_addsd_xx,
    [ADDSS]        = asm_addss_xx,
    [SUBSD]        = asm_subsd_xx,
    [SUBSS]        = asm_subss_xx,
    [MULSD]        = asm_mulsd_xx,
    [MULSS]        = asm_mulss_xx,
    [DIVSD]        = asm_divsd_xx,
    [DIVSS]        = asm_divss_xx,
    [XORPD]        = asm_xorpd_xx,
    [XORPD_IX]     = asm_xorpd_ix,
    [XORPS]        = asm_xorps_xx,
    [XORPS_IX]     = asm_xorps_ix,
    [COMISD]       = asm_comisd_xx,
    [COMISD_IX]    = asm_comisd_ix,
    [UCOMISD]      = asm_ucomisd_xx,
    [UCOMISD_IX]   = asm_ucomisd_ix,
    [COMISS]       = asm_comiss_xx,
    [COMISS_IX]    = asm_comiss_ix,
    [UCOMISS]      = asm_ucomiss_xx,
    [UCOMISS_IX]   = asm_ucomiss_ix,
    [CVTSI2SD]     = asm_cvtsi2sd_rx,
    [CVTSI2SS]     = asm_cvtsi2ss_rx,
    [CVTTSD2SI]    = asm_cvttsd2si_xr,
    [CVTTSS2SI]    = asm_cvttss2si_xr,
    [CVTSD2SS]     = asm_cvtsd2ss_xx,
    [CVTSS2SD]     = asm_cvtss2sd_xx,
    [SQRTSD]       = asm_sqrtsd_xx,
    [MOVQ_GP_XMM]  = asm_movq_gpxmm,
    [MOVQ_XMM_GP]  = asm_movq_xmmgp,
    [MOVQ_MEM_XMM] = asm_movq_ixmm,
    [MOVQ_XMM_MEM] = asm_movq_xmmi,
    [ENDBR64]      = asm_endbr64,

    /* no-operand instructions */
    [NOP]    = asm_simple,
    [PUSHFQ] = asm_simple,
    [POPFQ]  = asm_simple,
    [PUSHF]  = asm_simple,
    [POPF]   = asm_simple,
    [CPUID]  = asm_simple,
    [RDTSC]  = asm_simple,
    [UD2]    = asm_simple,
    [PAUSE]  = asm_simple,
    [HLT]    = asm_simple,
    [CMC]    = asm_simple,
    [CLC]    = asm_simple,
    [STC]    = asm_simple,
    [CLI]    = asm_simple,
    [STI]    = asm_simple,
    [CLD]    = asm_simple,
    [STD]    = asm_simple,
    [CLTS]   = asm_simple,
    [WBINVD] = asm_simple,
    [MFENCE] = asm_simple,
    [LFENCE] = asm_simple,
    [SFENCE] = asm_simple,

    /* register instructions */
    [IMUL]       = asm_imul_r,
    [IMUL_RR]    = asm_imul_rr,
    [IMUL_IRR]   = asm_imul_irr,
    [LEAVE]      = asm_leave,
    [ENTER]      = asm_enter,
    [XCHG_RR]    = asm_xchg_rr,
    [XCHG_RI]    = asm_xchg_ri,
    [BSF]        = asm_bsf,
    [BSWAP]      = asm_bswap,
    [XADD_RR]    = asm_xadd_rr,
    [XADD_RI]    = asm_xadd_ri,
    [CMPXCHG_RR] = asm_cmpxchg_rr,
    [CMPXCHG_RI] = asm_cmpxchg_ri,

    /* bit test */
    [BT_RR]  = asm_bt_family,
    [BTS_RR] = asm_bt_family,
    [BTR_RR] = asm_bt_family,
    [BTC_RR] = asm_bt_family,

    /* conditional move */
    [CMOVO]  = asm_cmov_rr,
    [CMOVNO] = asm_cmov_rr,
    [CMOVB]  = asm_cmov_rr,
    [CMOVAE] = asm_cmov_rr,
    [CMOVE]  = asm_cmov_rr,
    [CMOVNE] = asm_cmov_rr,
    [CMOVBE] = asm_cmov_rr,
    [CMOVA]  = asm_cmov_rr,
    [CMOVS]  = asm_cmov_rr,
    [CMOVNS] = asm_cmov_rr,
    [CMOVP]  = asm_cmov_rr,
    [CMOVNP] = asm_cmov_rr,
    [CMOVL]  = asm_cmov_rr,
    [CMOVGE] = asm_cmov_rr,
    [CMOVLE] = asm_cmov_rr,
    [CMOVG]  = asm_cmov_rr,

    /* string operations */
    [MOVSB] = asm_string,
    [MOVSW] = asm_string,
    [MOVSL] = asm_string,
    [MOVSQ] = asm_string,
    [STOSB] = asm_string,
    [STOSW] = asm_string,
    [STOSL] = asm_string,
    [STOSQ] = asm_string,
    [LODSB] = asm_string,
    [LODSW] = asm_string,
    [LODSL] = asm_string,
    [LODSQ] = asm_string,
    [SCASB] = asm_string,
    [SCASW] = asm_string,
    [SCASL] = asm_string,
    [SCASQ] = asm_string,
    [CMPSB] = asm_string,
    [CMPSW] = asm_string,
    [CMPSL] = asm_string,
    [CMPSQ] = asm_string,

    /* gas conversion aliases */
    [CBW]  = asm_cbw,
    [CDQE] = asm_cdqe,
    [CWD]  = asm_cwd,
};

void
assemble_inst(struct Inst *inst, struct ParseInfo *info, struct Code *code)
{
  code->flag = 0;
  code->len  = 0;

  unsigned char prefix = 0;
  switch (inst->prefix) {
  case 0:
    break;
  case PREFIX_LOCK:
    prefix = 0xf0;
    break;
  case PREFIX_REP:
    prefix = 0xf3;
    break;
  case PREFIX_REPNE:
    prefix = 0xf2;
    break;
  default:
    assemble_error(info, "invalid instruction prefix combination");
    return;
  }

  const AsmInstFunc *func = NULL;
  if (inst->op < (enum Opcode)ARRAY_SIZE(table)) {
    func = &table[inst->op];

    if (func != NULL) {
      unsigned char *p = (*func)(inst, code);
      if (p != NULL) {
        if (p == code->buf)
          p = code->buf + code->len;
        if (p > code->buf) {
          if (prefix != 0) {
            size_t len = p - code->buf;
            assert(len + 1 <= sizeof(code->buf));
            memmove(code->buf + 1, code->buf, len);
            code->buf[0] = prefix;
            p            = code->buf + 1 + len;
          }
          code->inst = inst;
          code->len  = p - code->buf;
          assert((size_t)code->len <= sizeof(code->buf));
        }
        return;
      }
    }
  }

  assemble_error(info, "Illegal operand");
}
