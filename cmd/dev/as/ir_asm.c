#include "../config.h"

#include "ir_asm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arch.h"
#include "parse_asm.h"
#include "table.h"
#include "tcutil.h"

/* ir constructors */

struct IR *
new_ir_label(const struct Name *label)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_LABEL;
  ir->label     = label;
  return ir;
}

struct IR *
new_ir_code(const struct Code *code)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_CODE;
  ir->code      = *code;
  return ir;
}

struct IR *
new_ir_data(const void *data, size_t size)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_DATA;
  ir->data.len  = size;
  ir->data.buf  = (unsigned char *)data;
  return ir;
}

struct IR *
new_ir_bss(size_t size)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_BSS;
  ir->bss       = size;
  return ir;
}

struct IR *
new_ir_zero(size_t size)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_ZERO;
  ir->zero      = size;
  return ir;
}

struct IR *
new_ir_align(int align)
{
  struct IR *ir = calloc_or_die(sizeof(*ir));
  ir->kind      = IR_ALIGN;
  ir->align     = align;
  return ir;
}

struct IR *
new_ir_expr(enum IrKind kind, const struct Expr *expr)
{
  struct IR *ir   = calloc_or_die(sizeof(*ir));
  ir->kind        = kind;
  ir->expr.expr   = expr;
  ir->expr.addend = 0;
  return ir;
}

/* section data helpers */

static void
sec_add_data(struct SectionInfo *section, const void *data, size_t bytes)
{
  assert(!(section->flag & SF_BSS));
  data_append(section->ds, data, bytes);
}

static void
sec_add_code(struct SectionInfo *section, const void *buf, size_t bytes)
{
  sec_add_data(section, buf, bytes);
}

static void
sec_add_bss(struct SectionInfo *section, size_t size)
{
  assert(section->flag & SF_BSS);
  section->bss_size += size;
}

static void
sec_align_size(struct SectionInfo *section, size_t align)
{
  if (align > section->align)
    section->align = align;

  if (section->flag & SF_BSS) {
    section->bss_size = ALIGN(section->bss_size, align);
  } else if (section->flag & SF_EXECUTABLE) {
    /* data_align pads with zero bytes, which decode as real (and
       wrong) instructions if control flow falls through them; pad
       with single-byte nops instead */
    size_t len         = section->ds->len;
    size_t aligned_len = ALIGN(len, align);
    if (aligned_len > len) {
      unsigned char *nops = malloc_or_die(aligned_len - len);
      memset(nops, 0x90, aligned_len - len);
      data_append(section->ds, nops, aligned_len - len);
      free(nops);
    }
  } else {
    data_align(section->ds, align);
  }
}

/* label address assignment */

int
calc_label_address(uint64_t start_address, struct Vector *sections, struct Table *label_table)
{
  int      settle  = 1;
  uint64_t address = start_address;
  for (int sec = 0; sec < sections->len; ++sec) {
    struct SectionInfo *section = sections->data[sec];
    address                     = ALIGN(address, section->align);
    section->start_address      = address;

    struct Vector *irs = section->irs;
    for (int i = 0, len = irs->len; i < len; ++i) {
      struct IR *ir = irs->data[i];
      ir->address   = address;
      switch (ir->kind) {
        case IR_LABEL: {
          struct LabelInfo *info;
          if (!table_try_get(label_table, ir->label, (void **)&info)) {
            fprintf(stderr, "[%.*s] not found\n", NAMES(ir->label));
            assert(!"unexpected");
          } else {
            info->address = address;
          }
        } break;
        case IR_CODE:
          address += ir->code.len;
          break;
        case IR_DATA:
          address += ir->data.len;
          break;
        case IR_BSS:
          address += ir->bss;
          break;
        case IR_ZERO:
          address += ir->zero;
          break;
        case IR_ALIGN:
          ir->address = address = ALIGN(address, ir->align);
          if ((size_t)ir->align > section->align) {
            section->align = ir->align;
            settle         = 0;
          }
          break;
        case IR_EXPR_BYTE:
          address += BYTE_SIZE;
          break;
        case IR_EXPR_SHORT:
          address += SHORT_SIZE;
          break;
        case IR_EXPR_LONG:
          address += LONG_SIZE;
          break;
        case IR_EXPR_QUAD:
          address += QUAD_SIZE;
          break;
      }
    }
  }
  return settle;
}

/* relative address resolution */

static void
put_value(unsigned char *p, int64_t value, int size)
{
  for (int i = 0; i < size; ++i) {
    *p++ = value;
    value >>= 8;
  }
}

int
resolve_relative_address(
    struct Vector *sections, struct Table *label_table, struct Vector *unresolved
)
{
  assert(unresolved != NULL);
  vec_clear(unresolved);
  int size_upgraded = 0;
  for (int sec = 0; sec < sections->len; ++sec) {
    struct SectionInfo *section       = sections->data[sec];
    struct Vector      *irs           = section->irs;
    uint64_t            start_address = irs->len > 0 ? ((struct IR *)irs->data[0])->address : 0;
    for (int i = 0, len = irs->len; i < len; ++i) {
      struct IR *ir      = irs->data[i];
      uint64_t   address = ir->address;
      switch (ir->kind) {
        case IR_CODE:
          arch.resolve_code_reloc(
              ir, section, address, start_address, label_table, unresolved, &size_upgraded
          );
          break;
        case IR_EXPR_BYTE:
        case IR_EXPR_SHORT:
        case IR_EXPR_LONG:
        case IR_EXPR_QUAD: {
          struct Value value = calc_expr(label_table, ir->expr.expr);
          assert(value.label != NULL);
          struct UnresolvedInfo *info = malloc_or_die(sizeof(*info));
          info->kind                  = UNRES_ABS64;
          info->label                 = value.label;
          info->src_section           = section;
          info->offset                = address - start_address;
          info->add                   = value.offset;
          ir->expr.addend             = value.offset;
          vec_push(unresolved, info);
        } break;
        case IR_LABEL:
        case IR_DATA:
        case IR_BSS:
        case IR_ZERO:
        case IR_ALIGN:
          break;
      }
    }
  }

  return !size_upgraded;
}

/* emit section data */

void
emit_irs(struct Vector *sections)
{
  for (int sec = 0; sec < sections->len; ++sec) {
    struct SectionInfo *section = sections->data[sec];
    struct Vector      *irs     = section->irs;
    for (int i = 0, len = irs->len; i < len; ++i) {
      struct IR *ir = irs->data[i];
      switch (ir->kind) {
        case IR_LABEL:
          break;
        case IR_CODE:
          sec_add_code(section, ir->code.buf, ir->code.len);
          break;
        case IR_DATA:
          sec_add_data(section, ir->data.buf, ir->data.len);
          break;
        case IR_BSS:
          sec_add_bss(section, ir->bss);
          break;
        case IR_ZERO:
          if (section->flag & SF_BSS) {
            sec_add_bss(section, ir->zero);
          } else {
            void *buf = calloc_or_die(ir->zero);
            sec_add_data(section, buf, ir->zero);
          }
          break;
        case IR_ALIGN:
          sec_align_size(section, ir->align);
          break;
        case IR_EXPR_BYTE:
        case IR_EXPR_SHORT:
        case IR_EXPR_LONG:
        case IR_EXPR_QUAD: {
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
          int64_t value = ir->expr.addend;
#else
          int64_t value = 0;
#endif
          int size = 1 << (ir->kind - IR_EXPR_BYTE);
          sec_add_data(section, &value, size);
        } break;
      }
    }
  }
}
