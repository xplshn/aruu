/* x64 instruction set definitions */

#ifndef AS_ARCH_X64_INST_H
#define AS_ARCH_X64_INST_H

#include "ir_asm.h" /* UNRES_ARCH_BASE */

#include <stdint.h> /* int64_t */

struct Expr;
struct Name;

/* x64-specific unresolved relocation kinds */
enum X64UnresolvedKind {
  UNRES_X64_ABS32S = UNRES_ARCH_BASE,
  UNRES_X64_ABS32,
  UNRES_X64_GOT_LOAD,
  UNRES_X64_TPOFF32,
  UNRES_X64_GOTTPOFF,
};

/* instruction prefix flags */
#define PREFIX_LOCK  (1 << 0)
#define PREFIX_REP   (1 << 1)
#define PREFIX_REPNE (1 << 2)

enum Opcode {
  NOOP,
  MOV_RR,
  MOV_IMR,
  MOV_IR,
  MOV_RI,
  MOV_IIR,
  MOV_RII,
  MOV_DR,
  MOV_RD,
  MOV_SR,
  MOVB_IMI,
  MOVW_IMI,
  MOVL_IMI,
  MOVQ_IMI,
  MOVB_IMD,
  MOVW_IMD,
  MOVL_IMD,
  MOVQ_IMD,
  MOVSX,
  MOVZX,
  /* sized extend forms. the gas mnemonics (movzbl, movswl, movslq, ...)
 * fix the source size in the mnemonic itself, which the encoder needs
 * when the source is a memory operand (a memory operand carries no
 * size). each form below names the source width and signedness so a
 * single encoder covers register and memory sources uniformly */
  MOVZX_B,
  MOVZX_W, /* zero-extend byte / word source */
  MOVSX_B,
  MOVSX_W,
  MOVSX_LQ, /* sign-extend byte / word / long(source) source */
  LEA_IR,
  LEA_IIR,

  ADD_RR,
  ADD_IMR,
  ADD_IR,
  ADD_IIR,
  ADDQ,
  SUB_RR,
  SUB_IMR,
  SUB_IR,
  SUB_IIR,
  SUBQ,
  MUL,
  IMUL,
  DIV,
  IDIV,
  NEG,
  NOT,
  INC,
  INCB,
  INCW,
  INCL,
  INCQ,
  DEC,
  DECB,
  DECW,
  DECL,
  DECQ,
  AND_RR,
  AND_IMR,
  OR_RR,
  OR_IMR,
  XOR_RR,
  XOR_IMR,
  SHL_RR,
  SHL_IMR,
  SHR_RR,
  SHR_IMR,
  SAR_RR,
  SAR_IMR,
  CMP_RR,
  CMP_IMR,
  TEST,
  XCHG_RR,
  XCHG_RI,
  CWTL,
  CLTD,
  CQTO,
  BSR,
  LZCNT,
  TZCNT,
  POPCNT,

  SETO,
  SETNO,
  SETB,
  SETAE,
  SETE,
  SETNE,
  SETBE,
  SETA,
  SETS,
  SETNS,
  SETP,
  SETNP,
  SETL,
  SETGE,
  SETLE,
  SETG,

  JMP_D,
  JMP_DER,
  JMP_DEI,
  JMP_DEII,
  JO,
  JNO,
  JB,
  JAE,
  JE,
  JNE,
  JBE,
  JA,
  JS,
  JNS,
  JP,
  JNP,
  JL,
  JGE,
  JLE,
  JG,
  CALL_D,
  CALL_DER,
  RET,
  LEAVE,
  PUSH_R,
  PUSH_IM,
  POP,

  INT,
  SYSCALL,

  MOVSD_XX,
  MOVSD_IX,
  MOVSD_XI,
  ADDSD,
  SUBSD,
  MULSD,
  DIVSD,
  XORPD,
  XORPD_IX,
  COMISD,
  COMISD_IX,
  UCOMISD,
  UCOMISD_IX,
  CVTSI2SD,
  CVTTSD2SI,
  SQRTSD,
  MOVQ_GP_XMM,
  MOVQ_XMM_GP,
  MOVQ_MEM_XMM,
  MOVQ_XMM_MEM,

  MOVSS_XX,
  MOVSS_IX,
  MOVSS_XI,
  ADDSS,
  SUBSS,
  MULSS,
  DIVSS,
  XORPS,
  XORPS_IX,
  COMISS,
  COMISS_IX,
  UCOMISS,
  UCOMISS_IX,
  CVTSI2SS,
  CVTTSS2SI,
  CVTSD2SS,
  CVTSS2SD,

  ENDBR64,

  /* no-operand instructions */
  NOP,
  PUSHFQ,
  POPFQ,
  PUSHF,
  POPF,
  CPUID,
  RDTSC,
  UD2,
  PAUSE,
  HLT,
  CMC,
  CLC,
  STC,
  CLI,
  STI,
  CLD,
  STD,
  CLTS,
  WBINVD,
  MFENCE,
  LFENCE,
  SFENCE,

  /* register instructions */
  IMUL_RR,
  IMUL_IRR,
  BSF,
  BSWAP,
  XADD_RR,
  XADD_RI,
  CMPXCHG_RR,
  CMPXCHG_RI,

  /* bit test family */
  BT_RR,
  BTS_RR,
  BTR_RR,
  BTC_RR,

  /* conditional move */
  CMOVO,
  CMOVNO,
  CMOVB,
  CMOVAE,
  CMOVE,
  CMOVNE,
  CMOVBE,
  CMOVA,
  CMOVS,
  CMOVNS,
  CMOVP,
  CMOVNP,
  CMOVL,
  CMOVGE,
  CMOVLE,
  CMOVG,

  /* string operations */
  MOVSB,
  MOVSW,
  MOVSL,
  MOVSQ,
  STOSB,
  STOSW,
  STOSL,
  STOSQ,
  LODSB,
  LODSW,
  LODSL,
  LODSQ,
  SCASB,
  SCASW,
  SCASL,
  SCASQ,
  CMPSB,
  CMPSW,
  CMPSL,
  CMPSQ,

  /* gas intel-syntax conversion aliases */
  CBW,
  CDQE,
  CWD,

  /* misc */
  ENTER,

  /* sse packed single-precision float (no prefix) */
  ADDPS,
  MULPS,
  SUBPS,
  MINPS,
  MAXPS,
  DIVPS,
  SQRTPS,
  RCPPS,
  RSQRTPS,
  ANDPS,
  ORPS,
  XORNPS,
  ANDNPS,
  CMPPS,
  MOVAPS_RR,
  MOVAPS_RM,
  MOVAPS_MR,
  MOVUPS_RR,
  MOVUPS_RM,
  MOVUPS_MR,
  HADDPS,
  HSUBPS,
  ADDSUBPS,

  /* sse2 packed double-precision float (66 prefix) */
  ADDPD,
  MULPD,
  SUBPD,
  MINPD,
  MAXPD,
  DIVPD,
  SQRTPD,
  ANDPD,
  ORPD,
  XORPD2,
  ANDNPD,
  CMPPD,
  MOVAPD_RR,
  MOVAPD_RM,
  MOVAPD_MR,
  MOVUPD_RR,
  MOVUPD_RM,
  MOVUPD_MR,
  HADDPD,
  HSUBPD,
  ADDSUBPD,

  /* sse2 packed integer (66 prefix) */
  PADDB,
  PADDW,
  PADDD,
  PADDQ,
  PSUBB,
  PSUBW,
  PSUBD,
  PSUBQ,
  PMULLW,
  PMULHW,
  PAND,
  POR,
  PXOR,
  PANDN,
  PCMPEQB,
  PCMPEQW,
  PCMPEQD,
  PCMPGTB,
  PCMPGTW,
  PCMPGTD,
  PSLLW_R,
  PSLLW_I,
  PSRLW_R,
  PSRLW_I,
  PSRAW_R,
  PSRAW_I,
  PSLLD_R,
  PSLLD_I,
  PSRLD_R,
  PSRLD_I,
  PSRAD_R,
  PSRAD_I,
  PSLLQ_R,
  PSLLQ_I,
  PSRLQ_R,
  PSRLQ_I,
  PMAXUB,
  PMINUB,
  PMAXSW,
  PMINSW,
  PSHUFD,
  PUNPCKLBW,
  PUNPCKLWD,
  PUNPCKLDQ,
  PUNPCKLQDQ,
  PUNPCKHBW,
  PUNPCKHWD,
  PUNPCKHDQ,
  PUNPCKHQDQ,
  PACKUSWB,
  PACKSSWB,
  PACKSSDW,
  MOVDQA_RR,
  MOVDQA_RM,
  MOVDQA_MR,
  MOVDQU_RR,
  MOVDQU_RM,
  MOVDQU_MR,
  PEXTRW,
  PINSRW,
  PMOVMSKB,

  /* avx packed single (vex, pp=00) */
  VADDPS,
  VMULPS,
  VSUBPS,
  VMINPS,
  VMAXPS,
  VDIVPS,
  VSQRTPS,
  VANDPS,
  VORPS,
  VXORPS,
  VMOVAPS,
  VMOVUPS,

  /* avx packed double (vex, pp=01) */
  VADDPD,
  VMULPD,
  VSUBPD,
  VMINPD,
  VMAXPD,
  VDIVPD,
  VSQRTPD,
  VANDPD,
  VORPD,
  VXORPD,
  VMOVAPD,
  VMOVUPD,

  /* avx packed integer (vex, pp=01) */
  VPADDB,
  VPADDW,
  VPADDD,
  VPADDQ,
  VPSUBB,
  VPSUBW,
  VPSUBD,
  VPSUBQ,
  VPAND,
  VPOR,
  VPXOR,
  VPANDN,
  VPCMPEQB,
  VPCMPEQW,
  VPCMPEQD,
  VPSLLW_I,
  VPSRLW_I,
  VPSRAW_I,
  VPSLLD_I,
  VPSRLD_I,
  VPSRAD_I,
  VPSLLQ_I,
  VPSRLQ_I,
  VPSHUFD,
  VMOVDQA,
  VMOVDQU,
  VPMAXUB,
  VPMINUB,
  VPMAXSW,
  VPMINSW,
  VPEXTRW,
  VPINSRW,
  VPMOVMSKB,

  /* avx misc */
  VZEROUPPER,
  VZEROALL,
  VBLENDPS,
  VBLENDPD,
  VBROADCASTSS,
  VBROADCASTSD,
  VBROADCASTF128,
};

enum RegType {
  NOREG = -1,

  /* 8bit */
  AL,
  CL,
  DL,
  BL,
  /* 8bit (high) */
  AH,
  CH,
  DH,
  BH,

  /* 8bit */
  R8B,
  R9B,
  R10B,
  R11B,
  R12B,
  R13B,
  R14B,
  R15B,

  /* 8bit: corresponds to AH~ in lower 4bit to handle easily */
  SPL = R15B + 1 + 4,
  BPL,
  SIL,
  DIL,

  /* 16bit */
  AX,
  CX,
  DX,
  BX,
  SP,
  BP,
  SI,
  DI,
  R8W,
  R9W,
  R10W,
  R11W,
  R12W,
  R13W,
  R14W,
  R15W,

  /* 32bit */
  EAX,
  ECX,
  EDX,
  EBX,
  ESP,
  EBP,
  ESI,
  EDI,
  R8D,
  R9D,
  R10D,
  R11D,
  R12D,
  R13D,
  R14D,
  R15D,

  /* 64bit */
  RAX,
  RCX,
  RDX,
  RBX,
  RSP,
  RBP,
  RSI,
  RDI,
  R8,
  R9,
  R10,
  R11,
  R12,
  R13,
  R14,
  R15,
  RIP,

  /* segment register */
  CS,
  DS,
  ES,
  FS,
  GS,
  SS,
};

enum RegXmmType {
  NOREGXMM = -1,
  XMM0,
  XMM1,
  XMM2,
  XMM3,
  XMM4,
  XMM5,
  XMM6,
  XMM7,
  XMM8,
  XMM9,
  XMM10,
  XMM11,
  XMM12,
  XMM13,
  XMM14,
  XMM15,
};

/* ymm registers share the same numbering as xmm, distinguished by
 * a separate type so the encoder knows the vector length */
enum RegYmmType {
  NOREGYMM = -1,
  YMM0,
  YMM1,
  YMM2,
  YMM3,
  YMM4,
  YMM5,
  YMM6,
  YMM7,
  YMM8,
  YMM9,
  YMM10,
  YMM11,
  YMM12,
  YMM13,
  YMM14,
  YMM15,
};

enum RegSize {
  REG8,
  REG16,
  REG32,
  REG64,
};

struct Reg {
  char size; /* regsize */
  char no;   /* 0~7, or RIP */
  char x;    /* 0 or 1, (or 2 if size == REG8, SPL~DIL) */
};

enum OperandType {
  NOOPERAND,
  REG,                       /* %rax */
  INDIRECT,                  /* ofs(%rax) */
  INDIRECT_WITH_INDEX,       /* ofs(%rax, %rcx, 4) */
  IMMEDIATE,                 /* $1234 */
  DIRECT,                    /* foobar */
  DEREF_REG,                 /* *%rax */
  DEREF_INDIRECT,            /* *ofs(%rax) */
  DEREF_INDIRECT_WITH_INDEX, /* *(%rax, %rcx, 4) */
  REG_XMM,
  REG_YMM,
  SEGMENT_OFFSET,
};

#define LF_GOTPCREL (1 << 0)
#define LF_TPOFF    (1 << 1) /* sym@tpoff, local-exec tls offset */
#define LF_GOTTPOFF (1 << 2) /* sym@gottpoff, initial-exec tls got slot */

struct ExprWithFlag {
  struct Expr *expr;
  int          flag;
};

struct Operand {
  enum OperandType type;
  union {
    struct Reg reg;
    int64_t    immediate;
    struct {
      struct Expr *expr;
    } direct;
    struct {
      struct ExprWithFlag offset;
      struct Reg          reg;
    } indirect;
    struct {
      struct Expr *offset;
      struct Expr *scale;
      struct Reg   base_reg;
      struct Reg   index_reg;
    } indirect_with_index;
    enum RegXmmType regxmm;
    enum RegYmmType regymm;
    struct {
      enum RegType         reg;
      struct ExprWithFlag  offset;
    } segment;
  };
};

struct Inst {
  enum Opcode    op;
  unsigned char  prefix;   /* PREFIX_LOCK, PREFIX_REP, etc */
  struct Operand opr[3];   /* src, dst, dst2 (for 3-operand vex) */
};

#endif /* AS_ARCH_X64_INST_H */
