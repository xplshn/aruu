#include "../../../config.h"

#include "arch.h"
#include "parse_asm.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "inst.h"
#include "tcutil.h"

enum RawOpcode {
  R_NOOP,
  R_MOV,
  R_MOVB,
  R_MOVW,
  R_MOVL,
  R_MOVQ,
  R_MOVSX,
  R_MOVZX,
  R_LEA,

  R_ADD,
  R_ADDQ,
  R_SUB,
  R_SUBQ,
  R_MUL,
  R_DIV,
  R_IDIV,
  R_NEG,
  R_NOT,
  R_INC,
  R_INCB,
  R_INCW,
  R_INCL,
  R_INCQ,
  R_DEC,
  R_DECB,
  R_DECW,
  R_DECL,
  R_DECQ,
  R_AND,
  R_OR,
  R_XOR,
  R_SHL,
  R_SHR,
  R_SAR,
  R_CMP,
  R_TEST,
  R_CWTL,
  R_CLTD,
  R_CQTO,
  R_BSR,
  R_LZCNT,
  R_TZCNT,
  R_POPCNT,

  R_SETO,
  R_SETNO,
  R_SETB,
  R_SETAE,
  R_SETE,
  R_SETNE,
  R_SETBE,
  R_SETA,
  R_SETS,
  R_SETNS,
  R_SETP,
  R_SETNP,
  R_SETL,
  R_SETGE,
  R_SETLE,
  R_SETG,

  R_JMP,
  R_JO,
  R_JNO,
  R_JB,
  R_JAE,
  R_JE,
  R_JNE,
  R_JBE,
  R_JA,
  R_JS,
  R_JNS,
  R_JP,
  R_JNP,
  R_JL,
  R_JGE,
  R_JLE,
  R_JG,
  R_CALL,
  R_RET,
  R_PUSH,
  R_POP,

  R_INT,
  R_SYSCALL,

  R_MOVSD,
  R_ADDSD,
  R_SUBSD,
  R_MULSD,
  R_DIVSD,
  R_XORPD,
  R_COMISD,
  R_UCOMISD,
  R_CVTSI2SD,
  R_CVTTSD2SI,
  R_SQRTSD,

  R_MOVSS,
  R_ADDSS,
  R_SUBSS,
  R_MULSS,
  R_DIVSS,
  R_XORPS,
  R_COMISS,
  R_UCOMISS,
  R_CVTSI2SS,
  R_CVTTSS2SI,
  R_CVTSD2SS,
  R_CVTSS2SD,

  R_ENDBR64,

  /* no-operand instructions */
  R_NOP,
  R_PUSHFQ,
  R_POPFQ,
  R_PUSHF,
  R_POPF,
  R_CPUID,
  R_RDTSC,
  R_UD2,
  R_PAUSE,
  R_HLT,
  R_CMC,
  R_CLC,
  R_STC,
  R_CLI,
  R_STI,
  R_CLD,
  R_STD,
  R_CLTS,
  R_WBINVD,
  R_MFENCE,
  R_LFENCE,
  R_SFENCE,

  /* register instructions */
  R_IMUL,
  R_BSF,
  R_BSWAP,
  R_XADD,
  R_CMPXCHG,
  R_LEAVE,
  R_ENTER,
  R_XCHG,

  /* bit test */
  R_BT,
  R_BTS,
  R_BTR,
  R_BTC,

  /* conditional move */
  R_CMOVO,
  R_CMOVNO,
  R_CMOVB,
  R_CMOVAE,
  R_CMOVE,
  R_CMOVNE,
  R_CMOVBE,
  R_CMOVA,
  R_CMOVS,
  R_CMOVNS,
  R_CMOVP,
  R_CMOVNP,
  R_CMOVL,
  R_CMOVGE,
  R_CMOVLE,
  R_CMOVG,

  /* string operations */
  R_MOVSB,
  R_MOVSW,
  R_MOVSL,
  R_MOVSQ,
  R_STOSB,
  R_STOSW,
  R_STOSL,
  R_STOSQ,
  R_LODSB,
  R_LODSW,
  R_LODSL,
  R_LODSQ,
  R_SCASB,
  R_SCASW,
  R_SCASL,
  R_SCASQ,
  R_CMPSB,
  R_CMPSW,
  R_CMPSL,
  R_CMPSQ,

  /* gas conversion aliases */
  R_CBW,
  R_CDQE,
  R_CWD,

  /* condition code aliases accepted by gas */
  R_JC,
  R_JNAE,
  R_JNB,
  R_JZ,
  R_JNZ,
  R_JNA,
  R_JNBE,
  R_JNGE,
  R_JNL,
  R_JNG,
  R_JNLE,
  R_JPE,
  R_JPO,
  R_CMOVZ,
  R_CMOVNZ,

  /* gas-style explicit size suffixes that qbe's amd64_sysv backend
   * emits, which this table otherwise only has for mov/inc/dec.
   * appended instead of inserted next to their unsuffixed siblings,
   * because kRawOpTable's array position is the enum value (see
   * find_raw_opcode's i + 1 return) and an insertion would shift every
   * later opcode */
  R_MOVSLQ,
  R_MOVSBW,
  R_MOVSBL,
  R_MOVSBQ,
  R_MOVSWL,
  R_MOVSWQ,
  R_MOVZBW,
  R_MOVZBL,
  R_MOVZBQ,
  R_MOVZWL,
  R_MOVZWQ,
  R_LEAQ,
  R_ADDL,
  R_SUBL,
  R_IDIVL,
  R_ANDL,
  R_ANDQ,
  R_ORL,
  R_ORQ,
  R_SHLL,
  R_SHLQ,
  R_SHRL,
  R_SHRQ,
  R_SARL,
  R_SARQ,
  R_CMPL,
  R_CMPQ,
  R_CALLQ,
  R_PUSHQ,
  R_POPQ,
  R_CVTTSD2SIL,
  R_CVTTSD2SIQ,
  R_CVTTSS2SIL,
  R_CVTTSS2SIQ,
  R_XCHGL,
  R_XCHGQ,
  R_SETZ,
  R_SETNZ,
  R_XORL,
  R_XORQ,
  R_NEGL,
  R_NEGQ,
};

const char *kRawOpTable[] = {
    "mov",
    "movb",
    "movw",
    "movl",
    "movq",
    "movsx",
    "movzx",
    "lea",

    "add",
    "addq",
    "sub",
    "subq",
    "mul",
    "div",
    "idiv",
    "neg",
    "not",
    "inc",
    "incb",
    "incw",
    "incl",
    "incq",
    "dec",
    "decb",
    "decw",
    "decl",
    "decq",
    "and",
    "or",
    "xor",
    "shl",
    "shr",
    "sar",
    "cmp",
    "test",
    "cwtl",
    "cltd",
    "cqto",
    "bsr",
    "lzcnt",
    "tzcnt",
    "popcnt",

    "seto",
    "setno",
    "setb",
    "setae",
    "sete",
    "setne",
    "setbe",
    "seta",
    "sets",
    "setns",
    "setp",
    "setnp",
    "setl",
    "setge",
    "setle",
    "setg",

    "jmp",
    "jo",
    "jno",
    "jb",
    "jae",
    "je",
    "jne",
    "jbe",
    "ja",
    "js",
    "jns",
    "jp",
    "jnp",
    "jl",
    "jge",
    "jle",
    "jg",
    "call",
    "ret",
    "push",
    "pop",

    "int",
    "syscall",

    "movsd",
    "addsd",
    "subsd",
    "mulsd",
    "divsd",
    "xorpd",
    "comisd",
    "ucomisd",
    "cvtsi2sd",
    "cvttsd2si",
    "sqrtsd",

    "movss",
    "addss",
    "subss",
    "mulss",
    "divss",
    "xorps",
    "comiss",
    "ucomiss",
    "cvtsi2ss",
    "cvttss2si",
    "cvtsd2ss",
    "cvtss2sd",

    "endbr64",

    /* no-operand */
    "nop",
    "pushfq",
    "popfq",
    "pushf",
    "popf",
    "cpuid",
    "rdtsc",
    "ud2",
    "pause",
    "hlt",
    "cmc",
    "clc",
    "stc",
    "cli",
    "sti",
    "cld",
    "std",
    "clts",
    "wbinvd",
    "mfence",
    "lfence",
    "sfence",

    /* register instructions */
    "imul",
    "bsf",
    "bswap",
    "xadd",
    "cmpxchg",
    "leave",
    "enter",
    "xchg",

    /* bit test */
    "bt",
    "bts",
    "btr",
    "btc",

    /* conditional move */
    "cmovo",
    "cmovno",
    "cmovb",
    "cmovae",
    "cmove",
    "cmovne",
    "cmovbe",
    "cmova",
    "cmovs",
    "cmovns",
    "cmovp",
    "cmovnp",
    "cmovl",
    "cmovge",
    "cmovle",
    "cmovg",

    /* string operations */
    "movsb",
    "movsw",
    "movsl",
    "movsq",
    "stosb",
    "stosw",
    "stosl",
    "stosq",
    "lodsb",
    "lodsw",
    "lodsl",
    "lodsq",
    "scasb",
    "scasw",
    "scasl",
    "scasq",
    "cmpsb",
    "cmpsw",
    "cmpsl",
    "cmpsq",

    /* gas conversion aliases */
    "cbw",
    "cdqe",
    "cwd",

    /* condition code aliases */
    "jc",
    "jnae",
    "jnb",
    "jz",
    "jnz",
    "jna",
    "jnbe",
    "jnge",
    "jnl",
    "jng",
    "jnle",
    "jpe",
    "jpo",
    "cmovz",
    "cmovnz",

    /* must stay in the same order as the R_* additions appended to
     * enum RawOpcode above, this array's position doubles as that
     * enum's value */
    "movslq",
    "movsbw",
    "movsbl",
    "movsbq",
    "movswl",
    "movswq",
    "movzbw",
    "movzbl",
    "movzbq",
    "movzwl",
    "movzwq",
    "leaq",
    "addl",
    "subl",
    "idivl",
    "andl",
    "andq",
    "orl",
    "orq",
    "shll",
    "shlq",
    "shrl",
    "shrq",
    "sarl",
    "sarq",
    "cmpl",
    "cmpq",
    "callq",
    "pushq",
    "popq",
    "cvttsd2sil",
    "cvttsd2siq",
    "cvttss2sil",
    "cvttss2siq",
    "xchgl",
    "xchgq",
    "setz",
    "setnz",
    "xorl",
    "xorq",
    "negl",
    "negq",
    NULL,
};

static const struct {
  const char  *name;
  enum RegType reg;
} kRegisters[] = {
    /* 8bit */
    {"al", AL},
    {"cl", CL},
    {"dl", DL},
    {"bl", BL},
    {"ah", AH},
    {"ch", CH},
    {"dh", DH},
    {"bh", BH},
    {"r8b", R8B},
    {"r9b", R9B},
    {"r10b", R10B},
    {"r11b", R11B},
    {"r12b", R12B},
    {"r13b", R13B},
    {"r14b", R14B},
    {"r15b", R15B},
    {"spl", SPL},
    {"bpl", BPL},
    {"sil", SIL},
    {"dil", DIL},

    /* 16bit */
    {"ax", AX},
    {"cx", CX},
    {"dx", DX},
    {"bx", BX},
    {"sp", SP},
    {"bp", BP},
    {"si", SI},
    {"di", DI},
    {"r8w", R8W},
    {"r9w", R9W},
    {"r10w", R10W},
    {"r11w", R11W},
    {"r12w", R12W},
    {"r13w", R13W},
    {"r14w", R14W},
    {"r15w", R15W},

    /* 32bit */
    {"eax", EAX},
    {"ecx", ECX},
    {"edx", EDX},
    {"ebx", EBX},
    {"esp", ESP},
    {"ebp", EBP},
    {"esi", ESI},
    {"edi", EDI},
    {"r8d", R8D},
    {"r9d", R9D},
    {"r10d", R10D},
    {"r11d", R11D},
    {"r12d", R12D},
    {"r13d", R13D},
    {"r14d", R14D},
    {"r15d", R15D},

    /* 64bit */
    {"rax", RAX},
    {"rcx", RCX},
    {"rdx", RDX},
    {"rbx", RBX},
    {"rsp", RSP},
    {"rbp", RBP},
    {"rsi", RSI},
    {"rdi", RDI},
    {"r8", R8},
    {"r9", R9},
    {"r10", R10},
    {"r11", R11},
    {"r12", R12},
    {"r13", R13},
    {"r14", R14},
    {"r15", R15},
    {"rip", RIP},

    /* segment register */
    {"cs", CS},
    {"ds", DS},
    {"es", ES},
    {"fs", FS},
    {"gs", GS},
    {"ss", SS},
};

static const char kXmmRegisters[][6] = {
    "xmm0",
    "xmm1",
    "xmm2",
    "xmm3",
    "xmm4",
    "xmm5",
    "xmm6",
    "xmm7",
    "xmm8",
    "xmm9",
    "xmm10",
    "xmm11",
    "xmm12",
    "xmm13",
    "xmm14",
    "xmm15",
};

static inline int
is_reg8(enum RegType reg)
{
  return reg >= AL && reg <= DIL;
}

static inline int
is_reg16(enum RegType reg)
{
  return reg >= AX && reg <= R15W;
}

static inline int
is_reg32(enum RegType reg)
{
  return reg >= EAX && reg <= R15D;
}

static inline int
is_reg64(enum RegType reg)
{
  return reg >= RAX && reg <= R15;
}

static inline int
is_segment(enum RegType reg)
{
  return reg >= CS && reg <= SS;
}

#define R8   (1 << 0)
#define R16  (1 << 1)
#define R32  (1 << 2)
#define R64  (1 << 3)
#define R8CL (1 << 4)
#define IMM  (1 << 5)
#define EXP  (1 << 6)
#define IND  (1 << 7)
#define IIND (1 << 8)
#define DER  (1 << 9)  /* deref reg64 */
#define DEI  (1 << 10) /* deref indirect */
#define DEII (1 << 11) /* deref indirect with index */
#define XMM  (1 << 12)
#define SEG  (1 << 13)
#define YMM  (1 << 14)

/* scan for x86 instruction prefix keywords */
unsigned char
x64_scan_prefix(struct ParseInfo *info)
{
  static const struct {
    const char  *name;
    unsigned char flag;
  } prefixes[] = {
      {"lock", PREFIX_LOCK},
      {"rep", PREFIX_REP},
      {"repe", PREFIX_REP},
      {"repz", PREFIX_REP},
      {"repne", PREFIX_REPNE},
      {"repnz", PREFIX_REPNE},
  };
  const char *p     = info->p;
  const char *start = p;
  while (isalnum(*p) || *p == '.')
    ++p;
  size_t n = p - start;
  if (n > 0 && (*p == '\0' || isspace(*p))) {
    for (int i = 0; i < (int)ARRAY_SIZE(prefixes); ++i) {
      if (n == strlen(prefixes[i].name) && strncasecmp(start, prefixes[i].name, n) == 0) {
        info->p = skip_whitespaces(p);
        return prefixes[i].flag;
      }
    }
  }
  return 0;
}

static enum RegType
find_register(const char **pp)
{
  const char *p = *pp;
  for (int i = 0; i < (int)ARRAY_SIZE(kRegisters); ++i) {
    const char *name = kRegisters[i].name;
    size_t      n    = strlen(name);
    if (strncasecmp(p, name, n) == 0 && !is_label_chr(p[n])) {
      *pp = p + n;
      return kRegisters[i].reg;
    }
  }
  return NOREG;
}

static enum RegXmmType
find_xmm_register(const char **pp)
{
  const char *p = *pp;
  const char *q;
  for (q = p; isalnum(*q); ++q)
    ;
  size_t l = q - p;

  for (int i = 0; i < (int)ARRAY_SIZE(kXmmRegisters); ++i) {
    const char *name = kXmmRegisters[i];
    size_t      n    = strlen(name);
    if (l == n && strncasecmp(p, name, n) == 0) {
      *pp = p + n;
      return i + XMM0;
    }
  }
  return NOREGXMM;
}

static unsigned int
parse_direct_register(struct ParseInfo *info, struct Operand *operand)
{
  {
    enum RegXmmType regxmm = find_xmm_register(&info->p);
    if (regxmm != NOREGXMM) {
      operand->type   = REG_XMM;
      operand->regxmm = regxmm;
      return XMM;
    }
  }

  enum RegType reg = find_register(&info->p);
  if (is_segment(reg)) {
    struct Expr *offset = NULL;
    int          flag   = 0;
    if (*info->p == ':') {
      ++info->p;
      offset = parse_expr(info);
      if (strncasecmp(info->p, "@tpoff", 6) == 0 && !is_label_chr(info->p[6])) {
        info->p += 6;
        flag = LF_TPOFF;
      }
    }
    operand->type                = SEGMENT_OFFSET;
    operand->segment.reg         = reg;
    operand->segment.offset.expr = offset;
    operand->segment.offset.flag = flag;
    return SEG;
  }

  enum RegSize size;
  int          no;
  if (is_reg8(reg)) {
    size = REG8;
    no   = reg - AL;
  } else if (is_reg16(reg)) {
    size = REG16;
    no   = reg - AX;
  } else if (is_reg32(reg)) {
    size = REG32;
    no   = reg - EAX;
  } else if (is_reg64(reg)) {
    size = REG64;
    no   = reg - RAX;
  } else {
    parse_error(info, "illegal register");
    return 0;
  }

  operand->type     = REG;
  operand->reg.size = size;
  operand->reg.no   = no & 7;
  operand->reg.x    = no >> 3;
  return (R8 << (size - REG8)) | (reg == CL ? R8CL : 0);
}

#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
static int
parse_label_postfix(struct ParseInfo *info)
{
  static struct {
    const char *name;
    int         flag;
  } const kPostfixes[] = {
      {"@gotpcrel", LF_GOTPCREL},
  };
  const char *p = info->p;
  for (size_t i = 0; i < ARRAY_SIZE(kPostfixes); ++i) {
    const char *name = kPostfixes[i].name;
    size_t      n    = strlen(name);
    if (strncasecmp(p, name, n) == 0 && !is_label_chr(p[n])) {
      info->p = p + n;
      return kPostfixes[i].flag;
    }
  }
  return 0;
}
#endif

static struct ExprWithFlag
parse_expr_with_flag(struct ParseInfo *info)
{
  /* expr = label + nn */
#if TC_TARGET_PLATFORM == TC_PLATFORM_APPLE
  /* expr@page */
  /* expr@pageoff */
  /* expr@gotpage */
  /* expr@gotpageoff */
  struct Expr *expr = parse_expr(info);
  int          flag = parse_label_postfix(info);
#else
  int          flag = 0;
  struct Expr *expr = parse_expr(info);
#endif
  return (struct ExprWithFlag){expr, flag};
}

static unsigned int
parse_indirect_register(
    struct ParseInfo *info, struct ExprWithFlag *offset, struct Operand *operand
)
{
  enum RegType index_reg = NOREG;
  struct Expr *scale     = NULL;
  /* already read "(%" */
  enum RegType base_reg = find_register(&info->p);

  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    if (*info->p != '%' || (++info->p, index_reg = find_register(&info->p), !is_reg64(index_reg)))
      parse_error(info, "register expected");
    info->p = skip_whitespaces(info->p);
    if (*info->p == ',') {
      info->p = skip_whitespaces(info->p + 1);
      scale   = parse_expr(info);
      if (scale->kind != EX_FIXNUM)
        parse_error(info, "constant value expected");
      info->p = skip_whitespaces(info->p);
    }
  }
  if (*info->p != ')')
    parse_error(info, "`)' expected");
  else
    ++info->p;

  if (!(is_reg64(base_reg) || (base_reg == RIP && index_reg == NOREG)))
    parse_error(info, "register expected");

  if (index_reg == NOREG) {
    char no                    = base_reg - RAX;
    operand->type              = INDIRECT;
    operand->indirect.reg.size = REG64;
    operand->indirect.reg.no   = base_reg != RIP ? no & 7 : RIP;
    operand->indirect.reg.x    = (no & 8) >> 3;
    operand->indirect.offset   = *offset;
    return IND;
  } else {
    if (!is_reg64(index_reg))
      parse_error(info, "register expected");

    operand->type                               = INDIRECT_WITH_INDEX;
    operand->indirect_with_index.offset         = offset->expr;
    operand->indirect_with_index.scale          = scale;
    char base_no                                = base_reg - RAX;
    operand->indirect_with_index.base_reg.size  = REG64;
    operand->indirect_with_index.base_reg.no    = base_no & 7;
    operand->indirect_with_index.base_reg.x     = (base_no & 8) >> 3;
    char index_no                               = index_reg - RAX;
    operand->indirect_with_index.index_reg.size = REG64;
    operand->indirect_with_index.index_reg.no   = index_no & 7;
    operand->indirect_with_index.index_reg.x    = (index_no & 8) >> 3;
    return IIND;
  }
}

static enum RegType
parse_deref_register(struct ParseInfo *info, struct Operand *operand)
{
  enum RegType reg = find_register(&info->p);
  if (!is_reg64(reg))
    parse_error(info, "illegal register");

  char no           = reg - RAX;
  operand->type     = DEREF_REG;
  operand->reg.size = REG64;
  operand->reg.no   = no & 7;
  operand->reg.x    = (no & 8) >> 3;
  return 1;
}

static unsigned int
parse_deref_indirect(struct ParseInfo *info, struct Operand *operand)
{
  struct Expr *offset = parse_expr(info);
  info->p             = skip_whitespaces(info->p);
  if (*info->p != '(') {
    parse_error(info, "direct number not implemented");
    return 0;
  }
  if (info->p[1] != '%') {
    parse_error(info, "register expected");
    return 0;
  }
  info->p += 2;

  enum RegType index_reg = NOREG;
  struct Expr *scale     = NULL;
  /* already read "(%" */
  enum RegType base_reg = find_register(&info->p);

  info->p = skip_whitespaces(info->p);
  if (*info->p == ',') {
    info->p = skip_whitespaces(info->p + 1);
    if (*info->p != '%' || (++info->p, index_reg = find_register(&info->p), !is_reg64(index_reg)))
      parse_error(info, "register expected");
    info->p = skip_whitespaces(info->p);
    if (*info->p == ',') {
      info->p = skip_whitespaces(info->p + 1);
      scale   = parse_expr(info);
      if (scale->kind != EX_FIXNUM)
        parse_error(info, "constant value expected");
      info->p = skip_whitespaces(info->p);
    }
  }
  if (*info->p != ')')
    parse_error(info, "`)' expected");
  else
    ++info->p;

  if (!is_reg64(base_reg) || (index_reg != NOREG && !is_reg64(index_reg)))
    parse_error(info, "register expected");

  if (index_reg == NOREG) {
    operand->type                 = DEREF_INDIRECT;
    operand->indirect.offset.expr = offset;
    operand->indirect.offset.flag = 0;
    char reg_no                   = base_reg - RAX;
    operand->indirect.reg.size    = REG64;
    operand->indirect.reg.no      = reg_no & 7;
    operand->indirect.reg.x       = (reg_no & 8) >> 3;
    return DEI;
  } else {
    operand->type                               = DEREF_INDIRECT_WITH_INDEX;
    operand->indirect_with_index.offset         = offset;
    operand->indirect_with_index.scale          = scale;
    char base_no                                = base_reg - RAX;
    operand->indirect_with_index.base_reg.size  = REG64;
    operand->indirect_with_index.base_reg.no    = base_no & 7;
    operand->indirect_with_index.base_reg.x     = (base_no & 8) >> 3;
    operand->indirect_with_index.index_reg.size = REG64;
    char index_no                               = index_reg - RAX;
    operand->indirect_with_index.index_reg.size = REG64;
    operand->indirect_with_index.index_reg.no   = index_no & 7;
    operand->indirect_with_index.index_reg.x    = (index_no & 8) >> 3;
    return DEII;
  }
}

unsigned int
parse_operand(struct ParseInfo *info, unsigned int opr_flag, struct Operand *operand)
{
  const char *p = info->p;
  if (opr_flag & (R8 | R16 | R32 | R64 | R8CL | XMM)) {
    if (*p == '%') {
      info->p = p + 1;
      return parse_direct_register(info, operand) & opr_flag;
    }
  }

  if (opr_flag & (DER | DEI)) {
    if (*p == '*') {
      if (opr_flag & DER) {
        if (p[1] == '%') {
          info->p = p + 2;
          if (parse_deref_register(info, operand))
            return DER;
        }
      }
      if (opr_flag & DEI) {
        info->p = p + 1;
        return parse_deref_indirect(info, operand);
      }
    }
  }

  if (opr_flag & IMM) {
    if (*p == '$') {
      info->p = p + 1;
      if (!immediate(&info->p, &operand->immediate))
        parse_error(info, "syntax error");
      operand->type = IMMEDIATE;
      return IMM;
    }
  }

  struct ExprWithFlag expr_with_flag = parse_expr_with_flag(info);
  if (*info->p == '@') {
    /* relocation suffix (@plt, @got, @gotpcrel, @gottpoff, ...): this
     * toolchain is static-only, so a direct call/jump target needs
     * nothing beyond the bare symbol. @gottpoff is the exception: an
     * initial-exec tls load through (%rip) is still a got lookup in a
     * static binary, so it is kept as a flag */
    if (strncasecmp(info->p, "@gottpoff", 9) == 0 && !is_label_chr(info->p[9])) {
      expr_with_flag.flag |= LF_GOTTPOFF;
      info->p += 9;
    } else {
      const char *q = info->p + 1;
      while (isalpha((unsigned char)*q) || isdigit((unsigned char)*q))
        ++q;
      info->p = q;
    }
  }
  info->p = skip_whitespaces(info->p);
  if (*info->p != '(') {
    if (expr_with_flag.expr != NULL) {
      if (expr_with_flag.expr->kind == EX_LABEL || expr_with_flag.expr->kind == EX_FIXNUM
          || expr_with_flag.expr->kind == EX_ADD || expr_with_flag.expr->kind == EX_SUB) {
        operand->type        = DIRECT;
        operand->direct.expr = expr_with_flag.expr;
        return EXP;
      }
      parse_error(info, "direct number not implemented");
    }
  } else {
    if (info->p[1] == '%') {
      info->p += 2;
      if (expr_with_flag.expr == NULL) {
        struct Expr *expr   = calloc_or_die(sizeof(*expr));
        expr->kind          = EX_FIXNUM;
        expr->fixnum        = 0;
        expr_with_flag.expr = expr;
      }
      return parse_indirect_register(info, &expr_with_flag, operand);
    }
  }

  return 0;
}

const struct ParseInstTable kParseInstTable[] = {
    [R_MOV] =
        {11,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOV_RR, {R8, R8}},
             &(struct ParseOpArray){MOV_RR, {R16, R16}},
             &(struct ParseOpArray){MOV_RR, {R32, R32}},
             &(struct ParseOpArray){MOV_RR, {R64, R64}},
             &(struct ParseOpArray){MOV_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){MOV_IR, {IND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){MOV_RI, {R8 | R16 | R32 | R64, IND}},
             &(struct ParseOpArray){MOV_IIR, {IIND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){MOV_DR, {EXP, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){MOV_RD, {R8 | R16 | R32 | R64, EXP}},
             &(struct ParseOpArray){MOV_SR, {SEG, R32 | R64}},
         }},
    [R_MOVB] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVB_IMI, {IMM, IND}},
             &(struct ParseOpArray){MOVB_IMD, {IMM, EXP}},
             &(struct ParseOpArray){MOV_IR, {IND, R8}},
             &(struct ParseOpArray){MOV_RI, {R8, IND}},
             &(struct ParseOpArray){MOV_RR, {R8, R8}},
             &(struct ParseOpArray){MOV_IMR, {IMM, R8}},
             &(struct ParseOpArray){MOV_IIR, {IIND, R8}},
         }},
    [R_MOVW] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVW_IMI, {IMM, IND}},
             &(struct ParseOpArray){MOVW_IMD, {IMM, EXP}},
             &(struct ParseOpArray){MOV_IR, {IND, R16}},
             &(struct ParseOpArray){MOV_RI, {R16, IND}},
             &(struct ParseOpArray){MOV_RR, {R16, R16}},
             &(struct ParseOpArray){MOV_IMR, {IMM, R16}},
             &(struct ParseOpArray){MOV_IIR, {IIND, R16}},
         }},
    [R_MOVL] =
        {8,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVL_IMI, {IMM, IND}},
             &(struct ParseOpArray){MOVL_IMD, {IMM, EXP}},
             &(struct ParseOpArray){MOV_IR, {IND, R32}},
             &(struct ParseOpArray){MOV_RI, {R32, IND}},
             &(struct ParseOpArray){MOV_RR, {R32, R32}},
             &(struct ParseOpArray){MOV_IMR, {IMM, R32}},
             &(struct ParseOpArray){MOV_IIR, {IIND, R32}},
             &(struct ParseOpArray){MOV_SR, {SEG, R32}},
         }},
    [R_MOVQ] =
        {12,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVQ_IMI, {IMM, IND}},
             &(struct ParseOpArray){MOVQ_IMD, {IMM, EXP}},
             &(struct ParseOpArray){MOV_IR, {IND, R64}},
             &(struct ParseOpArray){MOV_RI, {R64, IND}},
             &(struct ParseOpArray){MOV_RR, {R64, R64}},
             &(struct ParseOpArray){MOV_IMR, {IMM, R64}},
             &(struct ParseOpArray){MOV_IIR, {IIND, R64}},
             &(struct ParseOpArray){MOV_SR, {SEG, R64}},
             /* bitwise gp<->xmm move, qbe's idiom for spilling a
              * float through a gp register and for float negate's
              * sign-bit xor against a constant */
             &(struct ParseOpArray){MOVQ_GP_XMM, {R64, XMM}},
             &(struct ParseOpArray){MOVQ_XMM_GP, {XMM, R64}},
             &(struct ParseOpArray){MOVQ_MEM_XMM, {IND, XMM}},
             &(struct ParseOpArray){MOVQ_XMM_MEM, {XMM, IND}},
         }},
    [R_MOVSX] =
        {6,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVSX, {R8, R16}},
             &(struct ParseOpArray){MOVSX, {R8, R32}},
             &(struct ParseOpArray){MOVSX, {R8, R64}},
             &(struct ParseOpArray){MOVSX, {R16, R32}},
             &(struct ParseOpArray){MOVSX, {R16, R64}},
             &(struct ParseOpArray){MOVSX, {R32, R64}},
         }},
    [R_MOVZX] =
        {6,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVZX, {R8, R16}},
             &(struct ParseOpArray){MOVZX, {R8, R32}},
             &(struct ParseOpArray){MOVZX, {R8, R64}},
             &(struct ParseOpArray){MOVZX, {R16, R32}},
             &(struct ParseOpArray){MOVZX, {R16, R64}},
             &(struct ParseOpArray){MOVZX, {R32, R64}},
         }},
    [R_LEA] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){LEA_IR, {IND, R64}},
             &(struct ParseOpArray){LEA_IIR, {IIND, R64}},
         }},
    [R_ADD] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ADD_RR, {R8, R8}},
             &(struct ParseOpArray){ADD_RR, {R16, R16}},
             &(struct ParseOpArray){ADD_RR, {R32, R32}},
             &(struct ParseOpArray){ADD_RR, {R64, R64}},
             &(struct ParseOpArray){ADD_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){ADD_IR, {IND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){ADD_IIR, {IIND, R8 | R16 | R32 | R64}},
         }},
    [R_ADDQ] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ADDQ, {IMM, IND}},
             &(struct ParseOpArray){ADD_RR, {R8, R8}},
             &(struct ParseOpArray){ADD_RR, {R16, R16}},
             &(struct ParseOpArray){ADD_RR, {R32, R32}},
             &(struct ParseOpArray){ADD_RR, {R64, R64}},
             &(struct ParseOpArray){ADD_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){ADD_IR, {IND, R64}},
         }},
    [R_SUB] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SUB_RR, {R8, R8}},
             &(struct ParseOpArray){SUB_RR, {R16, R16}},
             &(struct ParseOpArray){SUB_RR, {R32, R32}},
             &(struct ParseOpArray){SUB_RR, {R64, R64}},
             &(struct ParseOpArray){SUB_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SUB_IR, {IND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SUB_IIR, {IIND, R8 | R16 | R32 | R64}},
         }},
    [R_SUBQ] =
        {6,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SUBQ, {IMM, IND}},
             &(struct ParseOpArray){SUB_RR, {R8, R8}},
             &(struct ParseOpArray){SUB_RR, {R16, R16}},
             &(struct ParseOpArray){SUB_RR, {R32, R32}},
             &(struct ParseOpArray){SUB_RR, {R64, R64}},
             &(struct ParseOpArray){SUB_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_MUL] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MUL, {R8 | R16 | R32 | R64}}}},
    [R_DIV] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DIV, {R8 | R16 | R32 | R64}}}},
    [R_IDIV] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){IDIV, {R8 | R16 | R32 | R64}}}},
    [R_NEG] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){NEG, {R8 | R16 | R32 | R64}}}},
    [R_NOT] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){NOT, {R8 | R16 | R32 | R64}}}},
    [R_INC] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INC, {R8 | R16 | R32 | R64}}}},
    [R_INCB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INCB, {IND}}}},
    [R_INCW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INCW, {IND}}}},
    [R_INCL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INCL, {IND}}}},
    [R_INCQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INCQ, {IND}}}},
    [R_DEC] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DEC, {R8 | R16 | R32 | R64}}}},
    [R_DECB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DECB, {IND}}}},
    [R_DECW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DECW, {IND}}}},
    [R_DECL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DECL, {IND}}}},
    [R_DECQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){DECQ, {IND}}}},
    [R_AND] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){AND_RR, {R8, R8}},
             &(struct ParseOpArray){AND_RR, {R16, R16}},
             &(struct ParseOpArray){AND_RR, {R32, R32}},
             &(struct ParseOpArray){AND_RR, {R64, R64}},
             &(struct ParseOpArray){AND_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_OR] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){OR_RR, {R8, R8}},
             &(struct ParseOpArray){OR_RR, {R16, R16}},
             &(struct ParseOpArray){OR_RR, {R32, R32}},
             &(struct ParseOpArray){OR_RR, {R64, R64}},
             &(struct ParseOpArray){OR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_XOR] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XOR_RR, {R8, R8}},
             &(struct ParseOpArray){XOR_RR, {R16, R16}},
             &(struct ParseOpArray){XOR_RR, {R32, R32}},
             &(struct ParseOpArray){XOR_RR, {R64, R64}},
             &(struct ParseOpArray){XOR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHL_RR, {R8CL, R8 | R16 | R32 | R64}}, /* %cl, %reg */
             &(struct ParseOpArray){SHL_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHR] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHR_RR, {R8CL, R8 | R16 | R32 | R64}}, /* %cl, %reg */
             &(struct ParseOpArray){SHR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SAR] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SAR_RR, {R8CL, R8 | R16 | R32 | R64}}, /* %cl, %reg */
             &(struct ParseOpArray){SAR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_CMP] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMP_RR, {R8, R8}},
             &(struct ParseOpArray){CMP_RR, {R16, R16}},
             &(struct ParseOpArray){CMP_RR, {R32, R32}},
             &(struct ParseOpArray){CMP_RR, {R64, R64}},
             &(struct ParseOpArray){CMP_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_TEST] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){TEST, {R8, R8}},
             &(struct ParseOpArray){TEST, {R16, R16}},
             &(struct ParseOpArray){TEST, {R32, R32}},
             &(struct ParseOpArray){TEST, {R64, R64}},
         }},
    [R_CWTL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CWTL}}},
    [R_CLTD] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CLTD}}},
    [R_CQTO] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CQTO}}},
    [R_BSR] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BSR, {R16, R16}},
             &(struct ParseOpArray){BSR, {R32, R32}},
             &(struct ParseOpArray){BSR, {R64, R64}},
         }},
    [R_LZCNT] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){LZCNT, {R16, R16}},
             &(struct ParseOpArray){LZCNT, {R32, R32}},
             &(struct ParseOpArray){LZCNT, {R64, R64}},
         }},
    [R_TZCNT] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){TZCNT, {R16, R16}},
             &(struct ParseOpArray){TZCNT, {R32, R32}},
             &(struct ParseOpArray){TZCNT, {R64, R64}},
         }},
    [R_POPCNT] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){POPCNT, {R16, R16}},
             &(struct ParseOpArray){POPCNT, {R32, R32}},
             &(struct ParseOpArray){POPCNT, {R64, R64}},
         }},
    [R_SETO]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETO, {R8}}}},
    [R_SETNO] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETNO, {R8}}}},
    [R_SETB]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETB, {R8}}}},
    [R_SETAE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETAE, {R8}}}},
    [R_SETE]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETE, {R8}}}},
    [R_SETNE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETNE, {R8}}}},
    /* setz/setnz: same encoding as sete/setne, gcc/qbe just prefer
     * this spelling for a zero-flag test not read as an equality */
    [R_SETZ]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETE, {R8}}}},
    [R_SETNZ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETNE, {R8}}}},
    [R_SETBE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETBE, {R8}}}},
    [R_SETA]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETA, {R8}}}},
    [R_SETS]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETS, {R8}}}},
    [R_SETNS] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETNS, {R8}}}},
    [R_SETP]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETP, {R8}}}},
    [R_SETNP] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETNP, {R8}}}},
    [R_SETL]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETL, {R8}}}},
    [R_SETGE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETGE, {R8}}}},
    [R_SETLE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETLE, {R8}}}},
    [R_SETG]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SETG, {R8}}}},
    [R_JMP] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){JMP_D, {EXP}},
             &(struct ParseOpArray){JMP_DER, {DER}},
             &(struct ParseOpArray){JMP_DEI, {DEI}},
             &(struct ParseOpArray){JMP_DEII, {DEII}},
         }},
    [R_JO]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JO, {EXP}}}},
    [R_JNO] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNO, {EXP}}}},
    [R_JB]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JB, {EXP}}}},
    [R_JAE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JAE, {EXP}}}},
    [R_JE]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JE, {EXP}}}},
    [R_JNE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNE, {EXP}}}},
    [R_JBE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JBE, {EXP}}}},
    [R_JA]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JA, {EXP}}}},
    [R_JS]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JS, {EXP}}}},
    [R_JNS] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNS, {EXP}}}},
    [R_JP]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JP, {EXP}}}},
    [R_JNP] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNP, {EXP}}}},
    [R_JL]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JL, {EXP}}}},
    [R_JGE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JGE, {EXP}}}},
    [R_JLE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JLE, {EXP}}}},
    [R_JG]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JG, {EXP}}}},
    [R_CALL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CALL_D, {EXP}},
             &(struct ParseOpArray){CALL_DER, {DER}},
         }},
    [R_RET] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){RET}}},
    [R_PUSH] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){PUSH_R, {R64}},
             &(struct ParseOpArray){PUSH_IM, {IMM}},
         }},
    [R_POP]     = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){POP, {R64}}}},
    [R_INT]     = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){INT, {IMM}}}},
    [R_SYSCALL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SYSCALL}}},

    [R_MOVSD] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVSD_XX, {XMM, XMM}},
             &(struct ParseOpArray){MOVSD_IX, {IND, XMM}},
             &(struct ParseOpArray){MOVSD_XI, {XMM, IND}},
         }},
    [R_MOVSS] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MOVSS_XX, {XMM, XMM}},
             &(struct ParseOpArray){MOVSS_IX, {IND, XMM}},
             &(struct ParseOpArray){MOVSS_XI, {XMM, IND}},
         }},
    [R_ADDSD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ADDSD, {XMM, XMM}},
         }},
    [R_ADDSS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ADDSS, {XMM, XMM}},
         }},
    [R_SUBSD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SUBSD, {XMM, XMM}},
         }},
    [R_SUBSS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SUBSS, {XMM, XMM}},
         }},
    [R_MULSD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MULSD, {XMM, XMM}},
         }},
    [R_MULSS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){MULSS, {XMM, XMM}},
         }},
    [R_DIVSD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){DIVSD, {XMM, XMM}},
         }},
    [R_DIVSS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){DIVSS, {XMM, XMM}},
         }},
    [R_XORPD] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XORPD, {XMM, XMM}},
             &(struct ParseOpArray){XORPD_IX, {IND, XMM}},
         }},
    [R_XORPS] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XORPS, {XMM, XMM}},
             &(struct ParseOpArray){XORPS_IX, {IND, XMM}},
         }},
    [R_COMISD] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){COMISD, {XMM, XMM}},
             &(struct ParseOpArray){COMISD_IX, {IND, XMM}},
         }},
    [R_UCOMISD] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){UCOMISD, {XMM, XMM}},
             &(struct ParseOpArray){UCOMISD_IX, {IND, XMM}},
         }},
    [R_COMISS] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){COMISS, {XMM, XMM}},
             &(struct ParseOpArray){COMISS_IX, {IND, XMM}},
         }},
    [R_UCOMISS] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){UCOMISS, {XMM, XMM}},
             &(struct ParseOpArray){UCOMISS_IX, {IND, XMM}},
         }},
    [R_CVTSI2SD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTSI2SD, {R32 | R64, XMM}},
         }},
    [R_CVTSI2SS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTSI2SS, {R32 | R64, XMM}},
         }},
    [R_CVTTSD2SI] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSD2SI, {XMM, R32 | R64}},
         }},
    [R_CVTTSS2SI] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSS2SI, {XMM, R32 | R64}},
         }},
    [R_CVTSD2SS] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTSD2SS, {XMM, XMM}},
         }},
    [R_CVTSS2SD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTSS2SD, {XMM, XMM}},
         }},
    [R_SQRTSD] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SQRTSD, {XMM, XMM}},
         }},

    [R_ENDBR64] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ENDBR64},
         }},

    /* no-operand instructions */
    [R_NOP]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){NOP}}},
    [R_PUSHFQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){PUSHFQ}}},
    [R_POPFQ]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){POPFQ}}},
    [R_PUSHF]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){PUSHF}}},
    [R_POPF]   = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){POPF}}},
    [R_CPUID]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CPUID}}},
    [R_RDTSC]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){RDTSC}}},
    [R_UD2]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){UD2}}},
    [R_PAUSE]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){PAUSE}}},
    [R_HLT]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){HLT}}},
    [R_CMC]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CMC}}},
    [R_CLC]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CLC}}},
    [R_STC]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STC}}},
    [R_CLI]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CLI}}},
    [R_STI]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STI}}},
    [R_CLD]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CLD}}},
    [R_STD]    = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STD}}},
    [R_CLTS]   = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CLTS}}},
    [R_WBINVD] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){WBINVD}}},
    [R_MFENCE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MFENCE}}},
    [R_LFENCE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LFENCE}}},
    [R_SFENCE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SFENCE}}},

    /* register instructions */
    [R_IMUL] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){IMUL, {R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){IMUL_RR, {R8 | R16 | R32 | R64, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){IMUL_IRR, {IMM, R16 | R32 | R64}},
         }},
    [R_BSF] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BSF, {R16, R16}},
             &(struct ParseOpArray){BSF, {R32, R32}},
             &(struct ParseOpArray){BSF, {R64, R64}},
         }},
    [R_BSWAP] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BSWAP, {R32}},
             &(struct ParseOpArray){BSWAP, {R64}},
         }},
    [R_XADD] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XADD_RR, {R8, R8}},
             &(struct ParseOpArray){XADD_RR, {R16, R16}},
             &(struct ParseOpArray){XADD_RR, {R32, R32}},
             &(struct ParseOpArray){XADD_RR, {R64, R64}},
             &(struct ParseOpArray){XADD_RI, {R32 | R64, IND}},
         }},
    [R_CMPXCHG] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMPXCHG_RR, {R8, R8}},
             &(struct ParseOpArray){CMPXCHG_RR, {R16, R16}},
             &(struct ParseOpArray){CMPXCHG_RR, {R32, R32}},
             &(struct ParseOpArray){CMPXCHG_RR, {R64, R64}},
             &(struct ParseOpArray){CMPXCHG_RI, {R32 | R64, IND}},
         }},
    [R_LEAVE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LEAVE}}},
    [R_ENTER] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){ENTER, {IMM, IMM}}}},
    [R_XCHG] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XCHG_RR, {R8, R8}},
             &(struct ParseOpArray){XCHG_RR, {R16, R16}},
             &(struct ParseOpArray){XCHG_RR, {R32, R32}},
             &(struct ParseOpArray){XCHG_RR, {R64, R64}},
             &(struct ParseOpArray){XCHG_RI, {R32 | R64, IND}},
         }},

    /* bit test */
    [R_BT] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BT_RR, {R16, R16}},
             &(struct ParseOpArray){BT_RR, {R32, R32}},
             &(struct ParseOpArray){BT_RR, {R64, R64}},
             &(struct ParseOpArray){BT_RR, {IMM, R16 | R32 | R64}},
         }},
    [R_BTS] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BTS_RR, {R16, R16}},
             &(struct ParseOpArray){BTS_RR, {R32, R32}},
             &(struct ParseOpArray){BTS_RR, {R64, R64}},
             &(struct ParseOpArray){BTS_RR, {IMM, R16 | R32 | R64}},
         }},
    [R_BTR] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BTR_RR, {R16, R16}},
             &(struct ParseOpArray){BTR_RR, {R32, R32}},
             &(struct ParseOpArray){BTR_RR, {R64, R64}},
             &(struct ParseOpArray){BTR_RR, {IMM, R16 | R32 | R64}},
         }},
    [R_BTC] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){BTC_RR, {R16, R16}},
             &(struct ParseOpArray){BTC_RR, {R32, R32}},
             &(struct ParseOpArray){BTC_RR, {R64, R64}},
             &(struct ParseOpArray){BTC_RR, {IMM, R16 | R32 | R64}},
         }},

    /* conditional move */
    [R_CMOVO] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVO, {R16, R16}},
             &(struct ParseOpArray){CMOVO, {R32, R32}},
             &(struct ParseOpArray){CMOVO, {R64, R64}}
         }},
    [R_CMOVNO] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVNO, {R16, R16}},
             &(struct ParseOpArray){CMOVNO, {R32, R32}},
             &(struct ParseOpArray){CMOVNO, {R64, R64}}
         }},
    [R_CMOVB] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVB, {R16, R16}},
             &(struct ParseOpArray){CMOVB, {R32, R32}},
             &(struct ParseOpArray){CMOVB, {R64, R64}}
         }},
    [R_CMOVAE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVAE, {R16, R16}},
             &(struct ParseOpArray){CMOVAE, {R32, R32}},
             &(struct ParseOpArray){CMOVAE, {R64, R64}}
         }},
    [R_CMOVE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVE, {R16, R16}},
             &(struct ParseOpArray){CMOVE, {R32, R32}},
             &(struct ParseOpArray){CMOVE, {R64, R64}}
         }},
    [R_CMOVNE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVNE, {R16, R16}},
             &(struct ParseOpArray){CMOVNE, {R32, R32}},
             &(struct ParseOpArray){CMOVNE, {R64, R64}}
         }},
    [R_CMOVBE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVBE, {R16, R16}},
             &(struct ParseOpArray){CMOVBE, {R32, R32}},
             &(struct ParseOpArray){CMOVBE, {R64, R64}}
         }},
    [R_CMOVA] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVA, {R16, R16}},
             &(struct ParseOpArray){CMOVA, {R32, R32}},
             &(struct ParseOpArray){CMOVA, {R64, R64}}
         }},
    [R_CMOVS] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVS, {R16, R16}},
             &(struct ParseOpArray){CMOVS, {R32, R32}},
             &(struct ParseOpArray){CMOVS, {R64, R64}}
         }},
    [R_CMOVNS] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVNS, {R16, R16}},
             &(struct ParseOpArray){CMOVNS, {R32, R32}},
             &(struct ParseOpArray){CMOVNS, {R64, R64}}
         }},
    [R_CMOVP] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVP, {R16, R16}},
             &(struct ParseOpArray){CMOVP, {R32, R32}},
             &(struct ParseOpArray){CMOVP, {R64, R64}}
         }},
    [R_CMOVNP] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVNP, {R16, R16}},
             &(struct ParseOpArray){CMOVNP, {R32, R32}},
             &(struct ParseOpArray){CMOVNP, {R64, R64}}
         }},
    [R_CMOVL] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVL, {R16, R16}},
             &(struct ParseOpArray){CMOVL, {R32, R32}},
             &(struct ParseOpArray){CMOVL, {R64, R64}}
         }},
    [R_CMOVGE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVGE, {R16, R16}},
             &(struct ParseOpArray){CMOVGE, {R32, R32}},
             &(struct ParseOpArray){CMOVGE, {R64, R64}}
         }},
    [R_CMOVLE] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVLE, {R16, R16}},
             &(struct ParseOpArray){CMOVLE, {R32, R32}},
             &(struct ParseOpArray){CMOVLE, {R64, R64}}
         }},
    [R_CMOVG] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVG, {R16, R16}},
             &(struct ParseOpArray){CMOVG, {R32, R32}},
             &(struct ParseOpArray){CMOVG, {R64, R64}}
         }},

    /* string operations */
    [R_MOVSB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSB}}},
    [R_MOVSW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSW}}},
    [R_MOVSL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSL}}},
    [R_MOVSQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSQ}}},
    [R_STOSB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STOSB}}},
    [R_STOSW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STOSW}}},
    [R_STOSL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STOSL}}},
    [R_STOSQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){STOSQ}}},
    [R_LODSB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LODSB}}},
    [R_LODSW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LODSW}}},
    [R_LODSL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LODSL}}},
    [R_LODSQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){LODSQ}}},
    [R_SCASB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SCASB}}},
    [R_SCASW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SCASW}}},
    [R_SCASL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SCASL}}},
    [R_SCASQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){SCASQ}}},
    [R_CMPSB] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CMPSB}}},
    [R_CMPSW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CMPSW}}},
    [R_CMPSL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CMPSL}}},
    [R_CMPSQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CMPSQ}}},

    /* gas conversion aliases */
    [R_CBW]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CBW}}},
    [R_CDQE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CDQE}}},
    [R_CWD]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){CWD}}},

    /* condition code aliases */
    [R_JC]   = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JB, {EXP}}}},
    [R_JNAE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JB, {EXP}}}},
    [R_JNB]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JAE, {EXP}}}},
    [R_JZ]   = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JE, {EXP}}}},
    [R_JNZ]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNE, {EXP}}}},
    [R_JNA]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JBE, {EXP}}}},
    [R_JNBE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JA, {EXP}}}},
    [R_JNGE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JL, {EXP}}}},
    [R_JNL]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JGE, {EXP}}}},
    [R_JNG]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JLE, {EXP}}}},
    [R_JNLE] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JG, {EXP}}}},
    [R_JPE]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JP, {EXP}}}},
    [R_JPO]  = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){JNP, {EXP}}}},

    [R_CMOVZ] =
        {3,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMOVE, {R16, R16}},
             &(struct ParseOpArray){CMOVE, {R32, R32}},
             &(struct ParseOpArray){CMOVE, {R64, R64}}
         }},
    [R_CMOVNZ] = {
        3,
        (const struct ParseOpArray *[]){
            &(struct ParseOpArray){CMOVNE, {R16, R16}},
            &(struct ParseOpArray){CMOVNE, {R32, R32}},
            &(struct ParseOpArray){CMOVNE, {R64, R64}}
        }
    },

    /* explicit-size gas mnemonics qbe's amd64_sysv backend emits: the
     * suffix only disambiguates operand width when its unsuffixed
     * sibling above cannot infer it from a register operand, so these
     * accept exactly the same operand patterns as that sibling */
    [R_LEAQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){LEA_IR, {IND, R64}},
             &(struct ParseOpArray){LEA_IIR, {IIND, R64}},
         }},
    [R_ADDL] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){ADD_RR, {R8, R8}},
             &(struct ParseOpArray){ADD_RR, {R16, R16}},
             &(struct ParseOpArray){ADD_RR, {R32, R32}},
             &(struct ParseOpArray){ADD_RR, {R64, R64}},
             &(struct ParseOpArray){ADD_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){ADD_IR, {IND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){ADD_IIR, {IIND, R8 | R16 | R32 | R64}},
         }},
    [R_SUBL] =
        {7,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SUB_RR, {R8, R8}},
             &(struct ParseOpArray){SUB_RR, {R16, R16}},
             &(struct ParseOpArray){SUB_RR, {R32, R32}},
             &(struct ParseOpArray){SUB_RR, {R64, R64}},
             &(struct ParseOpArray){SUB_IMR, {IMM, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SUB_IR, {IND, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SUB_IIR, {IIND, R8 | R16 | R32 | R64}},
         }},
    [R_IDIVL] =
        {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){IDIV, {R8 | R16 | R32 | R64}}}},
    [R_ANDL] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){AND_RR, {R8, R8}},
             &(struct ParseOpArray){AND_RR, {R16, R16}},
             &(struct ParseOpArray){AND_RR, {R32, R32}},
             &(struct ParseOpArray){AND_RR, {R64, R64}},
             &(struct ParseOpArray){AND_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_ANDQ] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){AND_RR, {R8, R8}},
             &(struct ParseOpArray){AND_RR, {R16, R16}},
             &(struct ParseOpArray){AND_RR, {R32, R32}},
             &(struct ParseOpArray){AND_RR, {R64, R64}},
             &(struct ParseOpArray){AND_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_ORL] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){OR_RR, {R8, R8}},
             &(struct ParseOpArray){OR_RR, {R16, R16}},
             &(struct ParseOpArray){OR_RR, {R32, R32}},
             &(struct ParseOpArray){OR_RR, {R64, R64}},
             &(struct ParseOpArray){OR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_ORQ] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){OR_RR, {R8, R8}},
             &(struct ParseOpArray){OR_RR, {R16, R16}},
             &(struct ParseOpArray){OR_RR, {R32, R32}},
             &(struct ParseOpArray){OR_RR, {R64, R64}},
             &(struct ParseOpArray){OR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHLL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHL_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SHL_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHLQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHL_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SHL_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHRL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHR_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SHR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SHRQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SHR_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SHR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SARL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SAR_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SAR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_SARQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){SAR_RR, {R8CL, R8 | R16 | R32 | R64}},
             &(struct ParseOpArray){SAR_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_CMPL] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMP_RR, {R8, R8}},
             &(struct ParseOpArray){CMP_RR, {R16, R16}},
             &(struct ParseOpArray){CMP_RR, {R32, R32}},
             &(struct ParseOpArray){CMP_RR, {R64, R64}},
             &(struct ParseOpArray){CMP_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_CMPQ] =
        {5,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CMP_RR, {R8, R8}},
             &(struct ParseOpArray){CMP_RR, {R16, R16}},
             &(struct ParseOpArray){CMP_RR, {R32, R32}},
             &(struct ParseOpArray){CMP_RR, {R64, R64}},
             &(struct ParseOpArray){CMP_IMR, {IMM, R8 | R16 | R32 | R64}},
         }},
    [R_CALLQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CALL_D, {EXP}},
             &(struct ParseOpArray){CALL_DER, {DER}},
         }},
    [R_PUSHQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){PUSH_R, {R64}},
             &(struct ParseOpArray){PUSH_IM, {IMM}},
         }},
    [R_POPQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){POP, {R64}}}},
    [R_CVTTSD2SIL] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSD2SI, {XMM, R32 | R64}},
         }},
    [R_CVTTSD2SIQ] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSD2SI, {XMM, R32 | R64}},
         }},
    [R_CVTTSS2SIL] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSS2SI, {XMM, R32 | R64}},
         }},
    [R_CVTTSS2SIQ] =
        {1,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){CVTTSS2SI, {XMM, R32 | R64}},
         }},
    [R_XCHGL] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XCHG_RR, {R8, R8}},
             &(struct ParseOpArray){XCHG_RR, {R16, R16}},
             &(struct ParseOpArray){XCHG_RR, {R32, R32}},
             &(struct ParseOpArray){XCHG_RR, {R64, R64}},
         }},
    [R_XCHGQ] =
        {4,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XCHG_RR, {R8, R8}},
             &(struct ParseOpArray){XCHG_RR, {R16, R16}},
             &(struct ParseOpArray){XCHG_RR, {R32, R32}},
             &(struct ParseOpArray){XCHG_RR, {R64, R64}},
         }},
    /* qbe emits the explicit-size spelling for both of these, not the
     * bare mnemonic, for reg-reg xor/neg, and also for xor against an
     * immediate (a boolean-negate idiom, `xorq $1, %reg`) */
    [R_XORL] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XOR_RR, {R32, R32}},
             &(struct ParseOpArray){XOR_IMR, {IMM, R32}},
         }},
    [R_XORQ] =
        {2,
         (const struct ParseOpArray *[]){
             &(struct ParseOpArray){XOR_RR, {R64, R64}},
             &(struct ParseOpArray){XOR_IMR, {IMM, R64}},
         }},
    [R_NEGL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){NEG, {R32}}}},
    [R_NEGQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){NEG, {R64}}}},
    /* asm_movszx_rr already handles every one of these size pairs, it
       was just never reachable: no raw opcode dispatched to it */
    [R_MOVSLQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R32, R64}}}},
    [R_MOVSBW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R8, R16}}}},
    [R_MOVSBL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R8, R32}}}},
    [R_MOVSBQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R8, R64}}}},
    [R_MOVSWL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R16, R32}}}},
    [R_MOVSWQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVSX, {R16, R64}}}},
    [R_MOVZBW] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVZX, {R8, R16}}}},
    [R_MOVZBL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVZX, {R8, R32}}}},
    [R_MOVZBQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVZX, {R8, R64}}}},
    [R_MOVZWL] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVZX, {R16, R32}}}},
    [R_MOVZWQ] = {1, (const struct ParseOpArray *[]){&(struct ParseOpArray){MOVZX, {R16, R64}}}},
};
