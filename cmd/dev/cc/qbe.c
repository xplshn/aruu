#include "util.h"
#include "cc.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct value {
  enum {
    VALUE_NONE,
    VALUE_GLOBAL,
    VALUE_INTCONST,
    VALUE_FLTCONST,
    VALUE_DBLCONST,
    VALUE_TEMP,
    VALUE_TYPE,
    VALUE_LABEL,

    VALUE_EXTERN = 1 << 4,
    VALUE_THREAD = 1 << 5,
    VALUE_QUOTE  = 1 << 6,
  } kind;
  unsigned id;
  union {
    char              *name;
    unsigned long long i;
    double             f;
  } u;
};

struct lvalue {
  struct value   *addr;
  struct bitfield bits;
};

enum instkind {
  INONE,

#define OP(op, name) op,
#include "ops.h"
#undef OP

  IARG,
  IVARARG,
};

struct qbetype {
  char          base, data;
  enum instkind load, store;
};

struct inst {
  enum instkind kind;
  int class;
  struct value res, *arg[2];
};

struct jump {
  enum {
    JUMP_NONE,
    JUMP_JMP,
    JUMP_JNZ,
    JUMP_RET,
    JUMP_HLT,
  } kind;
  struct value *arg;
  struct block *blk[2];
};

struct block {
  struct value label;
  struct array insts;
  struct {
    int class;
    struct block *blk[2];
    struct value *val[2];
    struct value  res;
  } phi;
  struct jump jump;

  struct block *next;
};

struct switchcase {
  struct treenode node;
  struct block   *body;
};

struct func {
  struct decl  *decl, *namedecl;
  char         *name;
  struct value *paramtemps;
  struct type  *type;
  struct block *start, *end;
  struct map    gotos;
  unsigned      lastid;
};

static const int ptrclass = 'l';

void
switchcase(struct switchcases *cases, unsigned long long i, struct block *b)
{
  struct switchcase *c;

  c = treeinsert(&cases->root, i, sizeof(*c));
  if (!c->node.new)
    error(&tok.loc, "multiple 'case' labels with same value");
  c->body = b;
}

/* values */

struct block *
mkblock(char *name)
{
  static unsigned id;
  struct block   *b;

  b               = xmalloc(sizeof(*b));
  b->label.kind   = VALUE_LABEL;
  b->label.u.name = name;
  b->label.id     = ++id;
  b->insts        = (struct array){0};
  b->jump.kind    = JUMP_NONE;
  b->phi.res.kind = VALUE_NONE;
  b->next         = NULL;

  return b;
}

struct value *
mkglobal(struct decl *d)
{
  static unsigned id;
  struct value   *v;

  v       = xmalloc(sizeof(*v));
  v->kind = VALUE_GLOBAL;
  if (d->linkage == LINKEXTERN)
    v->kind |= VALUE_EXTERN;
  if (d->kind == DECLOBJECT && d->u.obj.storage == SDTHREAD)
    v->kind |= VALUE_THREAD;
  if (d->asmname) {
    v->kind |= VALUE_QUOTE;
    v->u.name = d->asmname;
    v->id     = 0;
  } else {
    v->u.name = d->name;
    v->id     = d->linkage == LINKNONE ? ++id : 0;
  }

  return v;
}

/* extern reference to a plain-named runtime symbol with no struct decl
 * behind it, since nothing in the translation unit ever declares one:
 * the linker resolves it against aruu's own runtime (shared/libatomic
 * for the atomic builtins, shared/libint128 for __int128 multiply and
 * divide) */
static struct value *
mkextfn(const char *name)
{
  struct value *v = xmalloc(sizeof(*v));
  v->kind         = VALUE_GLOBAL | VALUE_EXTERN;
  v->id           = 0;
  v->u.name       = name;
  return v;
}

/* size-suffixed atomic runtime symbol (__atomic_load_4,
 * __atomic_fetch_add_8, ...). 4 and 8 byte pointees only so far */
static struct value *
mkatomicfn(struct type *pointee, const char *base)
{
  char *name;
  int   size = pointee->size;

  if (size != 4 && size != 8)
    error(&tok.loc, "atomic builtins on %d-byte types are not yet supported", size);
  name = xmalloc(strlen(base) + 3);
  sprintf(name, "%s_%d", base, size);
  return mkextfn(name);
}

struct value *
mkintconst(unsigned long long n)
{
  struct value *v;

  v       = xmalloc(sizeof(*v));
  v->kind = VALUE_INTCONST;
  v->u.i  = n;

  return v;
}

static struct value *
mkfltconst(int kind, double n)
{
  struct value *v;

  v       = xmalloc(sizeof(*v));
  v->kind = kind;
  v->u.f  = n;

  return v;
}

static struct qbetype
qbetype(struct type *t)
{
  static const struct qbetype ub = {'w', 'b', ILOADUB, ISTOREB}, sb = {'w', 'b', ILOADSB, ISTOREB},
                              uh = {'w', 'h', ILOADUH, ISTOREH}, sh = {'w', 'h', ILOADSH, ISTOREH},
                              w = {'w', 'w', ILOADW, ISTOREW}, l = {'l', 'l', ILOADL, ISTOREL},
                              s = {'s', 's', ILOADS, ISTORES}, d = {'d', 'd', ILOADD, ISTORED},
                              v = {0};

  if (t == &typevoid)
    return v;
  /* __int128 is arithmetic (PROPSCALAR) but, like a struct, has no
   * qbe class of its own: every __int128 value is an address (see
   * funcload/funcstore), so it takes the same l class an aggregate
   * gets below */
  if (!(t->prop & PROPSCALAR) || t->kind == TYPEINT128)
    return l;
  switch (t->size) {
    case 1:
      return t->u.arith.issigned ? sb : ub;
    case 2:
      return t->u.arith.issigned ? sh : uh;
    case 4:
      return t->prop & PROPFLOAT ? s : w;
    case 8:
      return t->prop & PROPFLOAT ? d : l;
    case 16:
      fatal("long double is not yet supported");
  }
  assert(0);
}

/* functions */

static void emittype(struct type *);
static void emitname(struct value *);
static void emitvalue(struct value *);

static void
functemp(struct func *f, struct value *v)
{
  v->kind   = VALUE_TEMP;
  v->u.name = NULL;
  v->id     = ++f->lastid;
}

static const char *const instname[] = {
#define OP(op, name) [op] = name,
#include "ops.h"
#undef OP
};

static struct inst *
mkinst(struct func *f, int op, int class, struct value *arg0, struct value *arg1)
{
  struct inst *inst;

  inst         = xmalloc(sizeof(*inst));
  inst->kind   = op;
  inst->class  = class;
  inst->arg[0] = arg0;
  inst->arg[1] = arg1;
  if (class && op != IARG)
    functemp(f, &inst->res);
  else
    inst->res.kind = VALUE_NONE;
  return inst;
}

static struct value *
funcinst(struct func *f, int op, int class, struct value *arg0, struct value *arg1)
{
  struct inst  *inst;
  struct block *b;

  if (f->end->jump.kind) {
    b = mkblock("dead");
    funclabel(f, b);
  }
  inst = mkinst(f, op, class, arg0, arg1);
  arrayaddptr(&f->end->insts, inst);
  return &inst->res;
}

static struct value *
funcbits(struct func *f, struct type *t, struct value *v, struct bitfield b)
{
  int class, bits;

  class = t->size <= 4 ? 'w' : 'l';
  bits  = b.after;
  if (!bits && t->kind == TYPEBITINT)
    bits = t->size * 8 - t->u.arith.width;
  if (bits) {
    bits += (t->size + 3 & -4) - t->size << 3;
    v = funcinst(f, ISHL, class, v, mkintconst(bits));
  }
  bits += b.before;
  if (bits)
    v = funcinst(f, t->u.arith.issigned ? ISAR : ISHR, class, v, mkintconst(bits));
  return v;
}

struct int128parts {
  struct value *lo, *hi;
};

/* a __int128 value is always an address (see funcload/funcstore), so
 * every operator on one starts by pulling its two 64 bit words out
 * of memory */
static struct int128parts
load128(struct func *f, struct value *addr)
{
  struct int128parts p;
  p.lo = funcinst(f, ILOADL, 'l', addr, NULL);
  p.hi = funcinst(f, ILOADL, 'l', funcinst(f, IADD, 'l', addr, mkintconst(8)), NULL);
  return p;
}

/* mirror of load128: stores lo/hi into a fresh 16 byte slot and
 * returns its address, the result every __int128 operator produces */
static struct value *
store128(struct func *f, struct value *lo, struct value *hi)
{
  struct value *addr = funcinst(f, IALLOC16, ptrclass, mkintconst(16), NULL);
  funcinst(f, ISTOREL, 0, lo, addr);
  funcinst(f, ISTOREL, 0, hi, funcinst(f, IADD, 'l', addr, mkintconst(8)));
  return addr;
}

struct int128cmp {
  struct value *lt, *eq; /* both 0 or 1, 'l' class */
};

/* branchless 128 bit compare: compares the high words first (signed
 * or unsigned per issigned) and only falls back to an unsigned low
 * word compare when the high words are equal, the standard technique
 * for ordering a multi-word integer with no way to chain a carry/
 * borrow flag between two independent qbe compare instructions */
static struct int128cmp
cmp128(struct func *f, struct int128parts a, struct int128parts b, int issigned)
{
  struct int128cmp c;
  struct value    *hi_lt, *hi_eq, *lo_ult;
  hi_lt  = funcinst(f, issigned ? ICSLTL : ICULTL, 'l', a.hi, b.hi);
  hi_eq  = funcinst(f, ICEQL, 'l', a.hi, b.hi);
  lo_ult = funcinst(f, ICULTL, 'l', a.lo, b.lo);
  c.lt   = funcinst(f, IOR, 'l', hi_lt, funcinst(f, IAND, 'l', hi_eq, lo_ult));
  c.eq   = funcinst(f, IAND, 'l', hi_eq, funcinst(f, ICEQL, 'l', a.lo, b.lo));
  return c;
}

/* __int128 multiply and divide both lower to a call into
 * shared/libint128/int128_x64.s: qbe has neither a widening multiply
 * nor any divide wider than 64 bits, and a real division needs a bit
 * serial long-division loop no ir-level codegen could express, so
 * both are out of line the same way atomics are (see mkatomicfn) */
static struct value *
funcmul128(struct func *f, struct value *l, struct value *r)
{
  struct value *fn, *v;

  emittype(&typeint128);
  fn = mkextfn("__multi3");
  v  = funcinst(f, ICALL, 'l', fn, typeint128.value);
  funcinst(f, IARG, 'l', l, typeint128.value);
  funcinst(f, IARG, 'l', r, typeint128.value);
  return v;
}

/* x + mask, then xor mask again: negates a two's complement value
 * when mask is all-ones, leaves it unchanged when mask is zero. used
 * both to take a signed __int128's absolute value (mask = its own
 * sign, replicated across both words) and, afterward, to flip a
 * division result's sign back without branching on it */
static struct int128parts
condneg128(struct func *f, struct int128parts a, struct value *mask)
{
  struct value      *sum_lo, *carry, *sum_hi;
  struct int128parts r;
  sum_lo = funcinst(f, IADD, 'l', a.lo, mask);
  carry  = funcinst(f, ICULTL, 'l', sum_lo, a.lo);
  sum_hi = funcinst(f, IADD, 'l', funcinst(f, IADD, 'l', a.hi, mask), carry);
  r.lo   = funcinst(f, IXOR, 'l', sum_lo, mask);
  r.hi   = funcinst(f, IXOR, 'l', sum_hi, mask);
  return r;
}

/* / and % on __int128 both call __udivmodti4, which only ever
 * operates on magnitudes: a signed division negates both operands to
 * their absolute value first (branchless, the standard mask = hi>>63
 * trick, reused afterward to flip the quotient's sign when the
 * operands' signs differed and the remainder's sign to match the
 * dividend, c's own rule for truncating division) */
static struct value *
funcdiv128(struct func *f, struct type *t, struct value *l, struct value *r, int want_remainder)
{
  struct int128parts a, b, ua, ub, qp, rp, res;
  struct value      *fn, *remslot, *uaaddr, *ubaddr, *q, *qmask, *rmask;

  a = load128(f, l);
  b = load128(f, r);
  if (t->u.arith.issigned) {
    struct value *amask = funcinst(f, ISAR, 'l', a.hi, mkintconst(63));
    struct value *bmask = funcinst(f, ISAR, 'l', b.hi, mkintconst(63));
    ua                  = condneg128(f, a, amask);
    ub                  = condneg128(f, b, bmask);
    qmask               = funcinst(f, IXOR, 'l', amask, bmask);
    rmask               = amask;
  } else {
    ua    = a;
    ub    = b;
    qmask = rmask = mkintconst(0);
  }

  emittype(&typeuint128);
  /* every value the call arguments reference must be computed before
   * the icall itself: emitinst's own call-printing groups an icall
   * with whatever iargs immediately follow it in the block's
   * instruction list, so nothing (store128's own alloc/store
   * instructions included) may land between them */
  remslot = funcinst(f, IALLOC16, ptrclass, mkintconst(16), NULL);
  uaaddr  = store128(f, ua.lo, ua.hi);
  ubaddr  = store128(f, ub.lo, ub.hi);
  fn      = mkextfn("__udivmodti4");
  q       = funcinst(f, ICALL, 'l', fn, typeuint128.value);
  funcinst(f, IARG, 'l', uaaddr, typeuint128.value);
  funcinst(f, IARG, 'l', ubaddr, typeuint128.value);
  funcinst(f, IARG, ptrclass, remslot, NULL);

  if (!want_remainder) {
    qp  = load128(f, q);
    res = condneg128(f, qp, qmask);
  } else {
    rp  = load128(f, remslot);
    res = condneg128(f, rp, rmask);
  }
  return store128(f, res.lo, res.hi);
}

/* variable count << and >> on __int128: n is a plain 64 bit count (the
 * shift count keeps its own promoted type instead of converting to
 * match the shifted operand, per c's own rule for shift operators, so
 * the caller widens it to 'l' before this point). qbe's shl/shr/sar
 * mask their count the same way real x86 does, so a shift by exactly
 * 64 (the one count where "shift the other word by 64-n" would itself
 * mean shift-by-zero, silently wrong) and a shift by zero (where the
 * complementary cross-word shift would itself mean shift-by-64) both
 * need to be routed around the general n-in-1..63 formula rather than
 * folded into it: two real branches, not a branchless trick */
static struct value *
funcshift128(struct func *f, struct type *t, struct value *l, struct value *n, int isleft)
{
  struct int128parts a = load128(f, l);
  struct block      *bzero, *bnz, *bsmall, *blarge, *bnzjoin, *bjoin;
  enum instkind      shrop = t->u.arith.issigned ? ISAR : ISHR;

  bzero = mkblock(isleft ? "shl128_zero" : "shr128_zero");
  bnz   = mkblock(isleft ? "shl128_nz" : "shr128_nz");
  bjoin = mkblock(isleft ? "shl128_join" : "shr128_join");
  funcjnz(f, funcinst(f, ICEQL, 'w', n, mkintconst(0)), NULL, bzero, bnz);

  funclabel(f, bzero);
  bjoin->phi.val[0] = l; /* shift by 0: reuse the operand's own address unchanged */
  bjoin->phi.blk[0] = f->end;
  funcjmp(f, bjoin);

  funclabel(f, bnz);
  bsmall  = mkblock(isleft ? "shl128_small" : "shr128_small");
  blarge  = mkblock(isleft ? "shl128_large" : "shr128_large");
  bnzjoin = mkblock(isleft ? "shl128_nzjoin" : "shr128_nzjoin");
  funcjnz(f, funcinst(f, ICULTL, 'w', n, mkintconst(64)), NULL, bsmall, blarge);

  funclabel(f, bsmall);
  {
    /* n in 1..63 here: the complementary shift (64-n) is 1..63 too,
     * never the 0-or-64 counts qbe/x86 would mask away */
    struct value *inv = funcinst(f, ISUB, 'l', mkintconst(64), n);
    struct value *lo, *hi;
    if (isleft) {
      hi =
          funcinst(f, IOR, 'l', funcinst(f, ISHL, 'l', a.hi, n), funcinst(f, ISHR, 'l', a.lo, inv));
      lo = funcinst(f, ISHL, 'l', a.lo, n);
    } else {
      lo =
          funcinst(f, IOR, 'l', funcinst(f, ISHR, 'l', a.lo, n), funcinst(f, ISHL, 'l', a.hi, inv));
      hi = funcinst(f, shrop, 'l', a.hi, n);
    }
    bnzjoin->phi.val[0] = store128(f, lo, hi);
    bnzjoin->phi.blk[0] = f->end;
    funcjmp(f, bnzjoin);
  }

  funclabel(f, blarge);
  {
    /* n in 64..127 (n >= 128 is ub, same as any other integer type's
     * shift by more than its width): the whole low word has already
     * shifted entirely into, or out of, the high word's position, so
     * only a single n-64 shift (0..63, safe) is needed */
    struct value *nn = funcinst(f, ISUB, 'l', n, mkintconst(64));
    struct value *lo, *hi;
    if (isleft) {
      hi = funcinst(f, ISHL, 'l', a.lo, nn);
      lo = mkintconst(0);
    } else {
      lo = funcinst(f, shrop, 'l', a.hi, nn);
      hi = t->u.arith.issigned ? funcinst(f, ISAR, 'l', a.hi, mkintconst(63)) : mkintconst(0);
    }
    bnzjoin->phi.val[1] = store128(f, lo, hi);
    bnzjoin->phi.blk[1] = f->end;
    funcjmp(f, bnzjoin);
  }

  funclabel(f, bnzjoin);
  bnzjoin->phi.class = 'l';
  functemp(f, &bnzjoin->phi.res);

  bjoin->phi.val[1] = &bnzjoin->phi.res;
  bjoin->phi.blk[1] = bnzjoin;
  funclabel(f, bjoin);
  bjoin->phi.class = 'l';
  functemp(f, &bjoin->phi.res);
  return &bjoin->phi.res;
}

struct complexparts {
  struct value *re, *im;
};

/* same reasoning as load128: a complex value is always an address
 * (see funcload/funcstore), so every operator on one starts by
 * pulling its real and imaginary parts, back to back in memory, out
 * of it. unlike __int128 the two halves are the underlying real
 * type's own qbe class ('s' or 'd'), not always 'l' */
static struct complexparts
loadcomplex(struct func *f, struct type *t, struct value *addr)
{
  struct qbetype      qt = qbetype(t->base);
  struct complexparts p;
  p.re = funcinst(f, qt.load, qt.base, addr, NULL);
  p.im =
      funcinst(f, qt.load, qt.base, funcinst(f, IADD, 'l', addr, mkintconst(t->base->size)), NULL);
  return p;
}

/* mirror of loadcomplex: stores re/im into a fresh slot sized and
 * aligned for t and returns its address, the result every complex
 * operator produces */
static struct value *
storecomplex(struct func *f, struct type *t, struct value *re, struct value *im)
{
  struct qbetype qt = qbetype(t->base);
  struct value  *addr =
      funcinst(f, t->align <= 4 ? IALLOC4 : IALLOC8, ptrclass, mkintconst(t->size), NULL);
  funcinst(f, qt.store, 0, re, addr);
  funcinst(f, qt.store, 0, im, funcinst(f, IADD, 'l', addr, mkintconst(t->base->size)));
  return addr;
}

static struct value *
convert(struct func *f, struct type *dst, struct type *src, struct value *l)
{
  enum instkind op;
  struct value *r = NULL;
  int class;

  if (src == dst)
    return l;
  if (src->kind == TYPEPOINTER)
    src = &typeulong;
  if (dst->kind == TYPEPOINTER)
    dst = &typeulong;
  if (dst->kind == TYPEVOID)
    return NULL;
  if (dst->kind == TYPEINT128 && src->kind == TYPEINT128)
    return l; /* signed <-> unsigned __int128: identical representation */
  if (dst->kind == TYPEINT128) {
    struct value *lo, *hi;
    int           issigned;
    if (src->prop & PROPFLOAT) {
      /* truncate toward zero through a 64 bit intermediate: magnitude
       * beyond 64 bits is lost, a documented gap rather than a
       * crash, the same treatment this compiler already gives any
       * other out-of-range float-to-int conversion */
      l        = convert(f, dst->u.arith.issigned ? &typelong : &typeulong, src, l);
      issigned = dst->u.arith.issigned;
    } else {
      issigned = src->u.arith.issigned;
      if (src->size < 8)
        l = convert(f, issigned ? &typelong : &typeulong, src, l);
    }
    lo = l;
    hi = issigned ? funcinst(f, ISAR, 'l', l, mkintconst(63)) : mkintconst(0);
    return store128(f, lo, hi);
  }
  if (src->kind == TYPEINT128) {
    struct int128parts p = load128(f, l);
    if (dst->kind == TYPEBOOL)
      return funcinst(f, ICNEL, 'w', funcinst(f, IOR, 'l', p.lo, p.hi), mkintconst(0));
    /* every other destination depends only on the low word: exact for
     * any value that fits, a documented magnitude loss otherwise,
     * left to the same implementation-defined narrowing this compiler
     * already leaves unchecked for every other lossy int conversion */
    return convert(f, dst, src->u.arith.issigned ? &typelong : &typeulong, p.lo);
  }
  if (dst->kind == TYPECOMPLEX && src->kind == TYPECOMPLEX) {
    /* float _Complex <-> double _Complex: convert each component on
     * its own, same as converting two independent reals */
    struct complexparts p = loadcomplex(f, src, l);
    return storecomplex(
        f, dst, convert(f, dst->base, src->base, p.re), convert(f, dst->base, src->base, p.im)
    );
  }
  if (dst->kind == TYPECOMPLEX) {
    /* real -> complex: imaginary part is exactly 0 */
    struct qbetype qt = qbetype(dst->base);
    return storecomplex(
        f,
        dst,
        convert(f, dst->base, src, l),
        mkfltconst(qt.base == 's' ? VALUE_FLTCONST : VALUE_DBLCONST, 0)
    );
  }
  if (src->kind == TYPECOMPLEX) {
    /* complex -> real: the imaginary part is simply discarded, the
     * same implementation-defined narrowing real compilers warn about
     * and this one does not */
    struct complexparts p = loadcomplex(f, src, l);
    return convert(f, dst, src->base, p.re);
  }
  if (!(src->prop & PROPREAL) || !(dst->prop & PROPREAL))
    fatal("internal error; unsupported conversion");
  if (dst->kind == TYPEBOOL) {
    class = 'w';
    if (src->prop & PROPINT) {
      r = mkintconst(0);
      switch (src->size) {
        case 1:
          op = ICNEW;
          if (src->kind != TYPEBITINT)
            l = funcinst(f, IEXTUB, 'w', l, NULL);
          break;
        case 2:
          op = ICNEW;
          if (src->kind != TYPEBITINT)
            l = funcinst(f, IEXTUH, 'w', l, NULL);
          break;
        case 4:
          op = ICNEW;
          break;
        case 8:
          op = ICNEL;
          break;
        default:
          fatal(
              "internal error; unknown integer "
              "conversion"
          );
      }
    } else {
      assert(src->prop & PROPFLOAT);
      switch (src->size) {
        case 4:
          op = ICNES, r = mkfltconst(VALUE_FLTCONST, 0);
          break;
        case 8:
          op = ICNED, r = mkfltconst(VALUE_DBLCONST, 0);
          break;
        default:
          fatal(
              "internal error; unknown floating point "
              "conversion"
          );
      }
    }
  } else if (dst->prop & PROPINT) {
    class = dst->size == 8 ? 'l' : 'w';
    if (src->prop & PROPINT) {
      if (dst->u.arith.width <= src->u.arith.width) {
        if (dst->kind == TYPEBITINT)
          return funcbits(f, dst, l, (struct bitfield){0});
        return l;
      }
      if (src->kind == TYPEBITINT && (src->size == 8 || dst->size == 4))
        return l;
      switch (src->size) {
        case 4:
          op = src->u.arith.issigned ? IEXTSW : IEXTUW;
          break;
        case 2:
          op = src->u.arith.issigned ? IEXTSH : IEXTUH;
          break;
        case 1:
          op = src->u.arith.issigned ? IEXTSB : IEXTUB;
          break;
        default:
          fatal(
              "internal error; unknown integer "
              "conversion"
          );
      }
    } else {
      if (dst->u.arith.issigned)
        op = src->size == 8 ? IDTOSI : ISTOSI;
      else
        op = src->size == 8 ? IDTOUI : ISTOUI;
    }
  } else {
    class = dst->size == 8 ? 'd' : 's';
    if (src->prop & PROPINT) {
      if (src->size < 4)
        l = convert(f, src->u.arith.issigned ? &typeint : &typeuint, src, l);
      if (src->u.arith.issigned)
        op = src->size == 8 ? ISLTOF : ISWTOF;
      else
        op = src->size == 8 ? IULTOF : IUWTOF;
    } else {
      assert(src->prop & PROPFLOAT);
      if (src->size == dst->size)
        return l;
      op = src->size < dst->size ? IEXTS : ITRUNCD;
    }
  }

  return funcinst(f, op, class, l, r);
}

static void
calcvla(struct func *f, struct type *t)
{
  struct value *length, *basesize;

  if (!(t->prop & PROPVM))
    return;
  if (t->base)
    calcvla(f, t->base);
  if (t->kind == TYPEFUNC || t->size)
    return;
  assert(t->kind == TYPEARRAY);
  if (!t->u.array.size) {
    assert(t->base->size || t->base->kind == TYPEARRAY);
    assert(t->u.array.length);
    length   = convert(f, &typeulong, t->u.array.length->type, funcexpr(f, t->u.array.length));
    basesize = t->base->size ? mkintconst(t->base->size) : t->base->u.array.size;
    t->u.array.size = funcinst(f, IMUL, 'l', length, basesize);
  }
}

static void
funcalloc(struct func *f, struct decl *d)
{
  enum instkind op;
  struct block *end;
  struct value *v;
  int           align;

  assert(!d->type->incomplete);
  calcvla(f, d->type);
  end = f->end;
  if (d->type->size) {
    f->end = f->start;
    v      = mkintconst(d->type->size);
  } else {
    assert(d->type->kind == TYPEARRAY);
    assert(d->type->u.array.size);
    v = d->type->u.array.size;
  }
  align = d->u.obj.align;
  switch (align) {
    case 1:
    case 2:
    case 4:
      op = IALLOC4;
      break;
    case 8:
      op = IALLOC8;
      break;
    default:
      v = funcinst(f, IADD, ptrclass, v, mkintconst(align - 16)); /* fallthrough */
    case 16:
      op = IALLOC16;
      break;
  }
  v = funcinst(f, op, ptrclass, v, NULL);
  if (align > 16) {
    /* TODO: implement alloc32 in QBE and use that instead */
    v = funcinst(f, IADD, ptrclass, v, mkintconst(align - 16));
    v = funcinst(f, IAND, ptrclass, v, mkintconst(-align));
  }
  d->value = v;
  f->end   = end;
}

static void
funccopy(struct func *f, struct value *dst, struct value *src, unsigned long long size, int align)
{
  enum instkind load, store;
  int class;
  struct value      *tmp, *inc;
  unsigned long long off;

  assert((align & align - 1) == 0);
  class = 'w';
  switch (align) {
    case 1:
      load = ILOADUB, store = ISTOREB;
      break;
    case 2:
      load = ILOADUH, store = ISTOREH;
      break;
    case 4:
      load = ILOADW, store = ISTOREW;
      break;
    default:
      load = ILOADL, store = ISTOREL, align = 8, class = 'l';
      break;
  }
  inc = mkintconst(align);
  off = 0;
  for (;;) {
    tmp = funcinst(f, load, class, src, NULL);
    funcinst(f, store, 0, tmp, dst);
    off += align;
    if (off >= size)
      break;
    src = funcinst(f, IADD, ptrclass, src, inc);
    dst = funcinst(f, IADD, ptrclass, dst, inc);
  }
}

static struct value *
funcstore(struct func *f, struct type *t, enum typequal tq, struct lvalue lval, struct value *v)
{
  struct value      *r;
  enum typeprop      tp;
  unsigned long long mask;
  struct qbetype     qt;
  int                bits;

  if (tq & QUALVOLATILE)
    error(&tok.loc, "volatile store is not yet supported");
  if (tq & QUALCONST)
    error(&tok.loc, "cannot store to 'const' object");
  tp = t->prop;
  assert(!lval.bits.before && !lval.bits.after || tp & PROPINT);
  r = v;
  switch (t->kind) {
    case TYPESTRUCT:
    case TYPEUNION:
    case TYPEARRAY:
    case TYPEINT128:
    case TYPECOMPLEX:
      funccopy(f, lval.addr, v, t->size, t->align);
      break;
    case TYPEPOINTER:
      t = &typeulong;
      /* fallthrough */
    default:
      assert(tp & PROPSCALAR);
      qt   = qbetype(t);
      bits = lval.bits.before + lval.bits.after;
      if (bits) {
        mask = 0xffffffffffffffffu >> 64 - t->size * 8 + bits << lval.bits.before;
        v    = funcinst(f, ISHL, qt.base, v, mkintconst(lval.bits.before));
        r    = funcbits(f, t, v, lval.bits);
        v    = funcinst(f, IAND, qt.base, v, mkintconst(mask));
        v    = funcinst(
            f,
            IOR,
            qt.base,
            v,
            funcinst(
                f, IAND, qt.base, funcinst(f, qt.load, qt.base, lval.addr, NULL), mkintconst(~mask)
            )
        );
      }
      funcinst(f, qt.store, 0, v, lval.addr);
      break;
  }
  return r;
}

static struct value *
funcload(struct func *f, struct type *t, struct lvalue lval)
{
  struct value  *v;
  struct qbetype qt;

  switch (t->kind) {
    case TYPESTRUCT:
    case TYPEUNION:
    case TYPEARRAY:
    case TYPEINT128:
    case TYPECOMPLEX:
      return lval.addr;
  }
  qt = qbetype(t);
  v  = funcinst(f, qt.load, qt.base, lval.addr, NULL);
  return funcbits(f, t, v, lval.bits);
}

struct func *
mkfunc(struct decl *decl, char *name, struct type *t, struct scope *s)
{
  struct func  *f;
  struct decl  *d;
  struct value *v;

  f        = xmalloc(sizeof(*f));
  f->decl  = decl;
  f->name  = name;
  f->type  = t;
  f->start = f->end = mkblock("start");
  f->lastid         = 0;
  mapinit(&f->gotos, 8);
  emittype(t->base);

  /* allocate space for parameters */
  f->paramtemps = xreallocarray(NULL, t->u.func.nparam, sizeof *f->paramtemps);
  for (d = t->u.func.params, v = f->paramtemps; d; d = d->next, ++v) {
    emittype(d->type);
    functemp(f, v);
    if (!d->name)
      continue;
    if (d->type->value) {
      d->value = v;
    } else {
      funcalloc(f, d);
      funcstore(f, d->type, QUALNONE, (struct lvalue){d->value}, v);
    }
  }

  t                = mkarraytype(&typechar, QUALCONST, strlen(name) + 1);
  d                = mkdecl("__func__", DECLOBJECT, t, QUALNONE, LINKNONE);
  d->u.obj.storage = SDSTATIC;
  d->value         = mkglobal(d);
  scopeputdecl(s, d);
  f->namedecl = d;

  funclabel(f, mkblock("body"));

  return f;
}

void
delfunc(struct func *f)
{
  struct block *b;
  struct inst **inst;

  while (b = f->start) {
    f->start = b->next;
    arrayforeach(&b->insts, inst) free(*inst);
    free(b->insts.val);
    free(b);
  }
  mapfree(&f->gotos, free);
  free(f);
}

struct type *
functype(struct func *f)
{
  return f->type;
}

void
funclabel(struct func *f, struct block *b)
{
  f->end->next = b;
  f->end       = b;
}

void
funcjmp(struct func *f, struct block *l)
{
  struct block *b = f->end;

  if (!b->jump.kind) {
    b->jump.kind   = JUMP_JMP;
    b->jump.blk[0] = l;
  }
}

void
funcjnz(struct func *f, struct value *v, struct type *t, struct block *l1, struct block *l2)
{
  struct block *b = f->end;

  if (b->jump.kind)
    return;
  if (t) {
    assert(t->prop & PROPSCALAR);
    /*
 * ideally we would just do this conversion unconditionally,
 * but QBE is not currently able to optimize the conversion
 * away for int
 */
    if (t->prop & PROPINT && t->size < 4)
      v = convert(f, &typeint, t, v);
    else if (t->prop & PROPFLOAT || t->size > 4)
      v = convert(f, &typebool, t, v);
  }
  b->jump.kind   = JUMP_JNZ;
  b->jump.arg    = v;
  b->jump.blk[0] = l1;
  b->jump.blk[1] = l2;
}

void
funcret(struct func *f, struct value *v)
{
  struct block *b = f->end;

  if (!b->jump.kind) {
    b->jump.kind = JUMP_RET;
    b->jump.arg  = v;
  }
}

void
funchlt(struct func *f)
{
  struct block *b = f->end;

  if (!b->jump.kind)
    b->jump.kind = JUMP_HLT;
}

struct gotolabel *
funcgoto(struct func *f, char *name)
{
  struct gotolabel *g;
  struct mapkey     key;
  size_t            idx;

  mapkey(&key, name, strlen(name));
  if (mapput(&f->gotos, &key, &idx)) {
    g                    = xmalloc(sizeof(*g));
    g->label             = mkblock(name);
    f->gotos.vals[idx].p = g;
  }

  return f->gotos.vals[idx].p;
}

static struct lvalue
funclval(struct func *f, struct expr *e)
{
  struct lvalue lval = {0};
  struct decl  *d;

  if (e->kind == EXPRBITFIELD) {
    lval.bits = e->u.bitfield.bits;
    e         = e->base;
  }
  switch (e->kind) {
    case EXPRIDENT:
      d = e->u.ident.decl;
      if (d->kind != DECLOBJECT && d->kind != DECLFUNC)
        error(&tok.loc, "identifier '%s' is not an object or function", d->name);
      if (d == f->namedecl) {
        fputs("data ", stdout);
        emitname(d->value);
        printf(" = { b \"%s\", b 0 }\n", f->name);
        f->namedecl = NULL;
      }
      lval.addr = d->value;
      break;
    case EXPRSTRING:
      d         = stringdecl(e);
      lval.addr = d->value;
      break;
    case EXPRCOMPOUND:
      if (e->toeval)
        funcexpr(f, e->toeval);
      d = e->u.compound.decl;
      funcinit(f, d, e->u.compound.init, true);
      lval.addr = d->value;
      break;
    case EXPRUNARY:
      if (e->op != TMUL)
        error(&tok.loc, "expression is not an object");
      lval.addr = funcexpr(f, e->base);
      break;
    default:
      if (e->type->kind != TYPESTRUCT && e->type->kind != TYPEUNION)
        error(&tok.loc, "expression is not an object");
      lval.addr = funcexpr(f, e);
  }
  return lval;
}

/* returns the expression value in the true branch */
struct value *
funcbranch(struct func *f, struct expr *e, struct block *bt, struct block *bf)
{
  static struct value one = {.kind = VALUE_INTCONST, .u.i = 1};
  struct expr        *l, *r;
  struct value       *v;
  struct block       *b;

  /* maybe we we could do something for EXPRCOND as well */
  switch (e->kind) {
    case EXPRBINARY:
      l = e->u.binary.l;
      r = e->u.binary.r;
      switch (e->op) {
        case TEQL:
        case TNEQ:
          r = eval(r);
          if (r->kind == EXPRCONST && r->type->prop & PROPINT && r->u.constant.u == 0) {
            if (e->op == TEQL)
              b = bt, bt = bf, bf = b;
            funcbranch(f, l, bt, bf);
            return &one;
          }
          break;
        case TLOR:
        case TLAND:
          if (e->op == TLOR) {
            b = mkblock("logic_or");
            funcbranch(f, l, bt, b);
          } else {
            b = mkblock("logic_and");
            funcbranch(f, l, b, bf);
          }
          funclabel(f, b);
          funcbranch(f, r, bt, bf);
          return &one;
      }
      break;
    case EXPRCOMMA:
      for (e = e->base; e->next; e = e->next)
        funcexpr(f, e);
      return funcbranch(f, e, bt, bf);
  }
  v = funcexpr(f, e);
  funcjnz(f, v, e->type, bt, bf);
  return v;
}

struct value *
funcexpr(struct func *f, struct expr *e)
{
  enum instkind op = INONE;
  struct decl  *d;
  struct value *l, *r, *v, *v2, *fn, **argvals;
  struct lvalue lval;
  struct expr  *arg;
  struct block *b[3];
  struct type  *t, *functype;
  size_t        i;

  calcvla(f, e->type);
  switch (e->kind) {
    case EXPRIDENT:
      d = e->u.ident.decl;
      switch (d->kind) {
        case DECLOBJECT:
          return funcload(f, e->type, (struct lvalue){d->value});
        case DECLCONST:
          return d->value;
        default:
          fatal("unimplemented declaration kind %d", d->kind);
      }
      break;
    case EXPRCONST:
      t = e->type;
      if (t->kind == TYPEINT128) {
        /* the frontend's own constant representation is 64 bits wide
         * (cc.h's expr.u.constant union), so every __int128 constant
         * that reaches here already fits in one word: build the
         * address load128/store128 callers expect by sign or zero
         * extending it to the high word, rather than handing back the
         * bare 64-bit scalar (which funcload/funcstore's own address
         * convention for this type would then misread as a pointer) */
        long long lo = e->u.constant.i;
        return store128(f, mkintconst(lo), mkintconst(t->u.arith.issigned && lo < 0 ? -1 : 0));
      }
      if (t->prop & PROPINT || t->kind == TYPEPOINTER)
        return mkintconst(e->u.constant.u);
      assert(t->prop & PROPFLOAT);
      return mkfltconst(t->size == 4 ? VALUE_FLTCONST : VALUE_DBLCONST, e->u.constant.f);
    case EXPRBITFIELD:
    case EXPRCOMPOUND:
      lval = funclval(f, e);
      return funcload(f, e->type, lval);
    case EXPRINCDEC:
      lval = funclval(f, e->base);
      l    = funcload(f, e->base->type, lval);
      t    = e->type;
      if (t->kind == TYPEPOINTER) {
        if (t->base->kind == TYPEARRAY && t->base->size == 0) {
          r = t->base->u.array.size;
        } else {
          r = mkintconst(t->base->size);
        }
      } else if (t->prop & PROPINT) {
        r = mkintconst(1);
      } else if (t->prop & PROPFLOAT) {
        r = mkfltconst(t->size == 4 ? VALUE_FLTCONST : VALUE_DBLCONST, 1);
      } else {
        fatal("not a scalar");
      }
      v = funcinst(f, e->op == TINC ? IADD : ISUB, qbetype(t).base, l, r);
      v = funcstore(f, e->type, e->qual, lval, v);
      return e->u.incdec.post ? l : v;
    case EXPRCALL:
      argvals = xreallocarray(NULL, e->u.call.nargs, sizeof(argvals[0]));
      for (arg = e->u.call.args, i = 0; arg; arg = arg->next, ++i) {
        emittype(arg->type);
        argvals[i] = funcexpr(f, arg);
      }
      t = e->type;
      emittype(t);
      v        = funcinst(f, ICALL, qbetype(t).base, funcexpr(f, e->base), t->value);
      functype = e->base->type->base;
      for (arg = e->u.call.args, i = 0; arg; arg = arg->next, ++i) {
        if (functype->u.func.isvararg && i == functype->u.func.nparam)
          funcinst(f, IVARARG, 0, NULL, NULL);
        t = arg->type;
        funcinst(f, IARG, qbetype(t).base, argvals[i], t->value);
      }
      e = e->base;
      if (e->kind == EXPRUNARY && e->op == TBAND) {
        e = e->base;
        if (e->kind == EXPRIDENT && e->u.ident.decl->u.func.isnoreturn)
          funchlt(f);
      }
      return v;
    case EXPRUNARY:
      switch (e->op) {
        case TBAND:
          lval = funclval(f, e->base);
          return lval.addr;
        case TMUL:
          r = funcexpr(f, e->base);
          return funcload(f, e->type, (struct lvalue){r});
        case TSUB:
          r = funcexpr(f, e->base);
          if (e->type->kind == TYPEINT128) {
            /* two's complement negation, same borrow logic as
             * TSUB's binary case with lo_a/hi_a fixed at zero */
            struct int128parts a;
            struct value      *lo, *hi, *carry;
            a     = load128(f, r);
            carry = funcinst(f, ICULTL, 'l', mkintconst(0), a.lo);
            lo    = funcinst(f, ISUB, 'l', mkintconst(0), a.lo);
            hi    = funcinst(f, ISUB, 'l', funcinst(f, ISUB, 'l', mkintconst(0), a.hi), carry);
            return store128(f, lo, hi);
          }
          if (e->type->kind == TYPECOMPLEX) {
            struct complexparts a;
            struct qbetype      qt;
            a  = loadcomplex(f, e->type, r);
            qt = qbetype(e->type->base);
            return storecomplex(
                f,
                e->type,
                funcinst(f, INEG, qt.base, a.re, NULL),
                funcinst(f, INEG, qt.base, a.im, NULL)
            );
          }
          return funcinst(f, INEG, qbetype(e->type).base, r, NULL);
      }
      fatal("internal error; unknown unary expression");
      break;
    case EXPRCAST:
      if (e->toeval)
        funcexpr(f, e->toeval);
      l = funcexpr(f, e->base);
      return convert(f, e->type, e->base->type, l);
    case EXPRBINARY:
      if (e->op == TLOR || e->op == TLAND) {
        b[0] = mkblock("logic_true");
        b[1] = mkblock("logic_false");
        b[2] = mkblock("logic_join");

        funcbranch(f, e, b[0], b[1]);
        funclabel(f, b[0]);
        funcjmp(f, b[2]);
        funclabel(f, b[1]);

        b[2]->phi.class  = 'w';
        b[2]->phi.blk[0] = b[0];
        b[2]->phi.val[0] = mkintconst(1);
        b[2]->phi.blk[1] = b[1];
        b[2]->phi.val[1] = mkintconst(0);
        functemp(f, &b[2]->phi.res);
        funclabel(f, b[2]);

        return &b[2]->phi.res;
      }
      l = funcexpr(f, e->u.binary.l);
      r = funcexpr(f, e->u.binary.r);
      t = e->u.binary.l->type;
      if (t->kind == TYPEPOINTER)
        t = &typeulong;
      if (t->kind == TYPEINT128) {
        /* qbe has no 128-bit register or op of its own, so a value
         * of this type always lives in memory (l/r are addresses,
         * see funcload/funcstore) and arithmetic works a word at a
         * time. add/sub need a carry/borrow between the low and high
         * words: (lo <u lo_a) after an add can only be true if the
         * add wrapped past 2**64, the standard software-carry trick
         * given no carry flag is reachable from qbe ir. multiply and
         * divide call out to shared/libint128 (see funcmul128/
         * funcdiv128) */
        struct int128parts a, b;
        struct value      *lo, *hi, *carry;
        if (e->op == TSHL || e->op == TSHR) {
          /* the shift count keeps its own promoted type rather than
           * converting to match the shifted operand (c's own rule
           * for shift operators), so r here is a plain scalar, never
           * a __int128 address like every other operand below */
          struct type  *rt = e->u.binary.r->type;
          struct value *n  = r;
          if (rt->kind == TYPEINT128)
            n = load128(f, r).lo;
          else if (rt->size < 8)
            n = funcinst(f, rt->u.arith.issigned ? IEXTSW : IEXTUW, 'l', r, NULL);
          return funcshift128(f, t, l, n, e->op == TSHL);
        }
        a = load128(f, l);
        b = load128(f, r);
        switch (e->op) {
          case TADD:
            lo    = funcinst(f, IADD, 'l', a.lo, b.lo);
            carry = funcinst(f, ICULTL, 'l', lo, a.lo);
            hi    = funcinst(f, IADD, 'l', funcinst(f, IADD, 'l', a.hi, b.hi), carry);
            break;
          case TSUB:
            carry = funcinst(f, ICULTL, 'l', a.lo, b.lo);
            lo    = funcinst(f, ISUB, 'l', a.lo, b.lo);
            hi    = funcinst(f, ISUB, 'l', funcinst(f, ISUB, 'l', a.hi, b.hi), carry);
            break;
          case TBAND:
            lo = funcinst(f, IAND, 'l', a.lo, b.lo);
            hi = funcinst(f, IAND, 'l', a.hi, b.hi);
            break;
          case TBOR:
            lo = funcinst(f, IOR, 'l', a.lo, b.lo);
            hi = funcinst(f, IOR, 'l', a.hi, b.hi);
            break;
          case TXOR:
            lo = funcinst(f, IXOR, 'l', a.lo, b.lo);
            hi = funcinst(f, IXOR, 'l', a.hi, b.hi);
            break;
          case TMUL:
            return funcmul128(f, l, r);
          case TDIV:
            return funcdiv128(f, t, l, r, 0);
          case TMOD:
            return funcdiv128(f, t, l, r, 1);
          case TLESS:
          case TGREATER:
          case TLEQ:
          case TGEQ:
          case TEQL:
          case TNEQ: {
            /* e->type here is plain int (a comparison's own result
             * type), not __int128, so this returns a scalar boolean
             * directly instead of falling through to the store128 at
             * the end of this case, which every arithmetic operator
             * above shares */
            int              issigned = t->u.arith.issigned;
            struct int128cmp c;
            struct value    *res;
            switch (e->op) {
              case TLESS:
                res = cmp128(f, a, b, issigned).lt;
                break;
              case TGREATER:
                res = cmp128(f, b, a, issigned).lt;
                break;
              case TLEQ:
                c   = cmp128(f, b, a, issigned);
                res = funcinst(f, IXOR, 'l', c.lt, mkintconst(1));
                break;
              case TGEQ:
                c   = cmp128(f, a, b, issigned);
                res = funcinst(f, IXOR, 'l', c.lt, mkintconst(1));
                break;
              case TEQL:
                res = cmp128(f, a, b, issigned).eq;
                break;
              default: /* TNEQ */
                c   = cmp128(f, a, b, issigned);
                res = funcinst(f, IXOR, 'l', c.eq, mkintconst(1));
                break;
            }
            return funcinst(f, ICNEL, 'w', res, mkintconst(0));
          }
          default:
            error(&tok.loc, "this operator is not yet supported for __int128");
            return NULL;
        }
        return store128(f, lo, hi);
      }
      if (t->kind == TYPECOMPLEX) {
        /* complex addition and subtraction are componentwise, no
         * carry propagation of any kind needed between the real and
         * imaginary parts (unlike __int128's words above). multiply
         * and divide use the ordinary cross-term formulas, real
         * scalar mul/add/sub qbe already has, no new runtime or
         * branching needed. == and != (the only orderings c defines
         * for a complex type) compare both components and return a
         * plain int, so those return early instead of falling
         * through to the storecomplex every arithmetic operator above
         * shares */
        struct complexparts a, b;
        struct value       *re, *im;
        struct qbetype      qt;
        a  = loadcomplex(f, t, l);
        b  = loadcomplex(f, t, r);
        qt = qbetype(t->base);
        switch (e->op) {
          case TADD:
            re = funcinst(f, IADD, qt.base, a.re, b.re);
            im = funcinst(f, IADD, qt.base, a.im, b.im);
            break;
          case TSUB:
            re = funcinst(f, ISUB, qt.base, a.re, b.re);
            im = funcinst(f, ISUB, qt.base, a.im, b.im);
            break;
          case TMUL:
            /* (a.re+a.im*i)(b.re+b.im*i) = (a.re*b.re - a.im*b.im) +
             * (a.re*b.im + a.im*b.re)*i */
            re = funcinst(
                f,
                ISUB,
                qt.base,
                funcinst(f, IMUL, qt.base, a.re, b.re),
                funcinst(f, IMUL, qt.base, a.im, b.im)
            );
            im = funcinst(
                f,
                IADD,
                qt.base,
                funcinst(f, IMUL, qt.base, a.re, b.im),
                funcinst(f, IMUL, qt.base, a.im, b.re)
            );
            break;
          case TDIV: {
            /* multiply num and denom by conj(b): denom becomes the
             * real scalar b.re^2+b.im^2, num becomes (a.re*b.re +
             * a.im*b.im) + (a.im*b.re - a.re*b.im)*i */
            struct value *denom = funcinst(
                f,
                IADD,
                qt.base,
                funcinst(f, IMUL, qt.base, b.re, b.re),
                funcinst(f, IMUL, qt.base, b.im, b.im)
            );
            struct value *numre = funcinst(
                f,
                IADD,
                qt.base,
                funcinst(f, IMUL, qt.base, a.re, b.re),
                funcinst(f, IMUL, qt.base, a.im, b.im)
            );
            struct value *numim = funcinst(
                f,
                ISUB,
                qt.base,
                funcinst(f, IMUL, qt.base, a.im, b.re),
                funcinst(f, IMUL, qt.base, a.re, b.im)
            );
            re = funcinst(f, IDIV, qt.base, numre, denom);
            im = funcinst(f, IDIV, qt.base, numim, denom);
            break;
          }
          case TEQL:
          case TNEQ: {
            enum instkind eqop = t->base->size == 4 ? ICEQS : ICEQD;
            struct value *req  = funcinst(f, eqop, 'w', a.re, b.re);
            struct value *ieq  = funcinst(f, eqop, 'w', a.im, b.im);
            struct value *both = funcinst(f, IAND, 'w', req, ieq);
            if (e->op == TNEQ)
              both = funcinst(f, IXOR, 'w', both, mkintconst(1));
            return funcinst(f, ICNEW, 'w', both, mkintconst(0));
          }
          default:
            error(&tok.loc, "this operator is not yet supported for complex types");
            return NULL;
        }
        return storecomplex(f, t, re, im);
      }
      switch (e->op) {
        case TMUL:
          op = IMUL;
          break;
        case TDIV:
          op = !(t->prop & PROPINT) || t->u.arith.issigned ? IDIV : IUDIV;
          break;
        case TMOD:
          op = t->u.arith.issigned ? IREM : IUREM;
          break;
        case TADD:
          op = IADD;
          break;
        case TSUB:
          op = ISUB;
          break;
        case TSHL:
          op = ISHL;
          break;
        case TSHR:
          op = t->u.arith.issigned ? ISAR : ISHR;
          break;
        case TBOR:
          op = IOR;
          break;
        case TBAND:
          op = IAND;
          break;
        case TXOR:
          op = IXOR;
          break;
        case TLESS:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICLTS : t->u.arith.issigned ? ICSLTW : ICULTW;
          else
            op = t->prop & PROPFLOAT ? ICLTD : t->u.arith.issigned ? ICSLTL : ICULTL;
          break;
        case TGREATER:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICGTS : t->u.arith.issigned ? ICSGTW : ICUGTW;
          else
            op = t->prop & PROPFLOAT ? ICGTD : t->u.arith.issigned ? ICSGTL : ICUGTL;
          break;
        case TLEQ:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICLES : t->u.arith.issigned ? ICSLEW : ICULEW;
          else
            op = t->prop & PROPFLOAT ? ICLED : t->u.arith.issigned ? ICSLEL : ICULEL;
          break;
        case TGEQ:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICGES : t->u.arith.issigned ? ICSGEW : ICUGEW;
          else
            op = t->prop & PROPFLOAT ? ICGED : t->u.arith.issigned ? ICSGEL : ICUGEL;
          break;
        case TEQL:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICEQS : ICEQW;
          else
            op = t->prop & PROPFLOAT ? ICEQD : ICEQL;
          break;
        case TNEQ:
          if (t->size <= 4)
            op = t->prop & PROPFLOAT ? ICNES : ICNEW;
          else
            op = t->prop & PROPFLOAT ? ICNED : ICNEL;
          break;
      }
      if (op == INONE)
        fatal(
            "internal error; unimplemented binary "
            "expression"
        );
      v = funcinst(f, op, qbetype(e->type).base, l, r);
      if (t->kind == TYPEBITINT)
        v = funcbits(f, t, v, (struct bitfield){0});
      return v;
    case EXPRCOND:
      b[0] = mkblock("cond_true");
      b[1] = mkblock("cond_false");
      b[2] = mkblock("cond_join");

      v = funcbranch(f, e->base, b[0], b[1]);

      funclabel(f, b[0]);
      if (e->u.cond.t != e->base)
        v = funcexpr(f, e->u.cond.t);
      b[2]->phi.val[0] = convert(f, e->type, e->u.cond.t->type, v);
      b[2]->phi.blk[0] = f->end;
      funcjmp(f, b[2]);

      funclabel(f, b[1]);
      v                = funcexpr(f, e->u.cond.f);
      b[2]->phi.val[1] = convert(f, e->type, e->u.cond.f->type, v);
      b[2]->phi.blk[1] = f->end;

      funclabel(f, b[2]);
      if (e->type == &typevoid)
        return NULL;
      functemp(f, &b[2]->phi.res);
      b[2]->phi.class = qbetype(e->type).base;
      return &b[2]->phi.res;
    case EXPRASSIGN:
      r = funcexpr(f, e->u.assign.r);
      if (e->u.assign.l->kind == EXPRTEMP) {
        e->u.assign.l->u.temp = r;
      } else {
        lval = funclval(f, e->u.assign.l);
        r    = funcstore(f, e->u.assign.l->type, e->u.assign.l->qual, lval, r);
      }
      return r;
    case EXPRCOMMA:
      for (e = e->base; e->next; e = e->next)
        funcexpr(f, e);
      return funcexpr(f, e);
    case EXPRBUILTIN:
      switch (e->u.builtin.kind) {
        case BUILTINVASTART:
          l = funcexpr(f, e->base);
          funcinst(f, IVASTART, 0, l, NULL);
          break;
        case BUILTINVAARG:
          if (e->toeval)
            funcexpr(f, e->toeval);
          /* https:// todo.sr.ht/~mcf/cproc/52 */
          if (!(e->type->prop & PROPSCALAR))
            error(
                &tok.loc,
                "va_arg with non-scalar type "
                "is not yet supported"
            );
          l = funcexpr(f, e->base);
          return funcinst(f, IVAARG, qbetype(e->type).base, l, NULL);
        case BUILTINALLOCA:
          l = funcexpr(f, e->base);
          return funcinst(f, IALLOC16, ptrclass, l, NULL);
        case BUILTINUNREACHABLE:
          funchlt(f);
          return NULL;
        case BUILTINATOMICLOAD:
          t  = e->base->type->base;
          l  = funcexpr(f, e->base);
          fn = mkatomicfn(t, "__atomic_load");
          v  = funcinst(f, ICALL, qbetype(t).base, fn, NULL);
          funcinst(f, IARG, ptrclass, l, NULL);
          return v;
        case BUILTINATOMICSTORE:
          t  = e->base->type->base;
          l  = funcexpr(f, e->base);
          r  = funcexpr(f, e->u.builtin.val);
          fn = mkatomicfn(t, "__atomic_store");
          funcinst(f, ICALL, 0, fn, NULL);
          funcinst(f, IARG, ptrclass, l, NULL);
          funcinst(f, IARG, qbetype(t).base, r, NULL);
          return NULL;
        case BUILTINATOMICEXCHANGE:
          t  = e->base->type->base;
          l  = funcexpr(f, e->base);
          r  = funcexpr(f, e->u.builtin.val);
          fn = mkatomicfn(t, "__atomic_exchange");
          v  = funcinst(f, ICALL, qbetype(t).base, fn, NULL);
          funcinst(f, IARG, ptrclass, l, NULL);
          funcinst(f, IARG, qbetype(t).base, r, NULL);
          return v;
        case BUILTINATOMICCOMPAREEXCHANGE:
          t  = e->base->type->base;
          l  = funcexpr(f, e->base);
          v2 = funcexpr(f, e->u.builtin.val2);
          r  = funcexpr(f, e->u.builtin.val);
          fn = mkatomicfn(t, "__atomic_compare_exchange");
          v  = funcinst(f, ICALL, qbetype(e->type).base, fn, NULL);
          funcinst(f, IARG, ptrclass, l, NULL);
          funcinst(f, IARG, ptrclass, v2, NULL);
          funcinst(f, IARG, qbetype(t).base, r, NULL);
          return v;
        case BUILTINATOMICFETCHADD:
        case BUILTINATOMICFETCHSUB:
        case BUILTINATOMICFETCHAND:
        case BUILTINATOMICFETCHOR:
        case BUILTINATOMICFETCHXOR:
          t = e->base->type->base;
          l = funcexpr(f, e->base);
          r = funcexpr(f, e->u.builtin.val);
          switch (e->u.builtin.kind) {
            case BUILTINATOMICFETCHADD:
              fn = mkatomicfn(t, "__atomic_fetch_add");
              break;
            case BUILTINATOMICFETCHSUB:
              fn = mkatomicfn(t, "__atomic_fetch_sub");
              break;
            case BUILTINATOMICFETCHAND:
              fn = mkatomicfn(t, "__atomic_fetch_and");
              break;
            case BUILTINATOMICFETCHOR: fn = mkatomicfn(t, "__atomic_fetch_or"); break;
            default: fn = mkatomicfn(t, "__atomic_fetch_xor"); break;
          }
          v = funcinst(f, ICALL, qbetype(t).base, fn, NULL);
          funcinst(f, IARG, ptrclass, l, NULL);
          funcinst(f, IARG, qbetype(t).base, r, NULL);
          return v;
        default:
          fatal("internal error: unimplemented builtin");
      }
      return NULL;
    case EXPRTEMP:
      assert(e->u.temp);
      return e->u.temp;
    case EXPRSIZEOF:
      t = e->u.szof.type;
      assert(t->kind == TYPEARRAY);
      calcvla(f, t);
      /* if the sizeof operand has VLA type, we must evaluate it */
      if (e->base)
        funcexpr(f, e->base);
      return t->u.array.size;
  }
  fatal("unimplemented expression %d", e->kind);
  return NULL;
}

static void
zero(
    struct func       *func,
    struct value      *addr,
    int                align,
    unsigned long long offset,
    unsigned long long end
)
{
  static const enum instkind store[] = {
      [1] = ISTOREB,
      [2] = ISTOREH,
      [4] = ISTOREW,
      [8] = ISTOREL,
  };
  static struct value z = {.kind = VALUE_INTCONST};
  struct value       *tmp;
  int                 a = 1;

  while (offset < end) {
    if ((align - (offset & align - 1)) & a) {
      tmp = offset ? funcinst(func, IADD, ptrclass, addr, mkintconst(offset)) : addr;
      funcinst(func, store[a], 0, &z, tmp);
      offset += a;
    }
    if (a < align)
      a <<= 1;
  }
}

void
funcinit(struct func *func, struct decl *d, struct init *init, bool hasinit)
{
  struct lvalue      dst;
  struct value      *src, *v;
  unsigned long long offset = 0, max = 0;
  size_t             i, w;

  funcalloc(func, d);
  if (!hasinit)
    return;
  for (; init; init = init->next) {
    zero(func, d->value, d->type->align, offset, init->start);
    dst.bits = init->bits;
    if (init->expr->kind == EXPRSTRING) {
      w = init->expr->type->base->size;
      for (i = 0; i < init->expr->u.string.size && i * w < init->end - init->start; ++i) {
        v        = mkintconst(init->start + i * w);
        dst.addr = funcinst(func, IADD, ptrclass, d->value, v);
        switch (w) {
          case 1:
            v = mkintconst(((unsigned char *)init->expr->u.string.data)[i]);
            break;
          case 2:
            v = mkintconst(((uint_least16_t *)init->expr->u.string.data)[i]);
            break;
          case 4:
            v = mkintconst(((uint_least32_t *)init->expr->u.string.data)[i]);
            break;
        }
        funcstore(func, init->expr->type->base, QUALNONE, dst, v);
      }
      offset = init->start + i * w;
    } else {
      if (offset < init->end && (dst.bits.before || dst.bits.after))
        zero(func, d->value, d->type->align, offset, init->end);
      dst.addr = d->value;
      /*
 * qbe's memopt does not eliminate the store for ptr + 0,
 * so only emit the add if the offset is non-zero
 */
      if (init->start > 0)
        dst.addr = funcinst(func, IADD, ptrclass, dst.addr, mkintconst(init->start));
      src = funcexpr(func, init->expr);
      funcstore(func, init->expr->type, QUALNONE, dst, src);
      offset = init->end;
    }
    if (max < offset)
      max = offset;
  }
  zero(func, d->value, d->type->align, max, d->type->size);
}

static void
casesearch(
    struct func *f,
    int class,
    struct value      *v,
    struct switchcase *c,
    struct block      *defaultlabel,
    unsigned long long min,
    unsigned long long max
)
{
  struct value *res, *key;
  struct block *label[3];

  if (!c) {
    funcjmp(f, defaultlabel);
    return;
  }
  if (min == max) {
    funcjmp(f, c->body);
    return;
  }
  label[0] = mkblock("switch_ne");
  label[1] = mkblock("switch_lt");
  label[2] = mkblock("switch_gt");

  key = mkintconst(c->node.key);
  res = funcinst(f, class == 'w' ? ICEQW : ICEQL, 'w', v, key);
  funcjnz(f, res, NULL, c->body, label[0]);
  funclabel(f, label[0]);
  res = funcinst(f, class == 'w' ? ICULTW : ICULTL, 'w', v, key);
  funcjnz(f, res, NULL, label[1], label[2]);
  funclabel(f, label[1]);
  casesearch(f, class, v, c->node.child[0], defaultlabel, min, c->node.key - 1);
  funclabel(f, label[2]);
  casesearch(f, class, v, c->node.child[1], defaultlabel, c->node.key + 1, max);
}

void
funcswitch(struct func *f, struct value *v, struct switchcases *c, struct block *defaultlabel)
{
  casesearch(f, qbetype(c->type).base, v, c->root, defaultlabel, 0, -1);
}

/* emit */

static void
emitname(struct value *v)
{
  static const char sigil[] = {
      [VALUE_TEMP]   = '%',
      [VALUE_GLOBAL] = '$',
      [VALUE_TYPE]   = ':',
      [VALUE_LABEL]  = '@',
  };
  int kind;

  kind = v->kind & 0xf;
  if (kind >= countof(sigil) || !sigil[kind])
    fatal("invalid value");
  putchar(sigil[kind]);
  if (kind == VALUE_GLOBAL && v->id)
    fputs(".L", stdout);
  if (v->kind & VALUE_QUOTE)
    putchar('"');
  if (v->u.name)
    fputs(v->u.name, stdout);
  if (v->kind & VALUE_QUOTE)
    putchar('"');
  if (v->id)
    printf(".%u", v->id);
}

static void
emitvalue(struct value *v)
{
  switch (v->kind & 0xf) {
    case VALUE_INTCONST:
      printf("%llu", v->u.i);
      break;
    case VALUE_FLTCONST:
      printf("s_%.17g", v->u.f);
      break;
    case VALUE_DBLCONST:
      printf("d_%.17g", v->u.f);
      break;
    case VALUE_GLOBAL:
      if (v->kind & VALUE_EXTERN)
        fputs("extern ", stdout);
      if (v->kind & VALUE_THREAD)
        fputs("thread ", stdout);
      /* fallthrough */
    default:
      emitname(v);
      break;
  }
}

static void
emitclass(int class, struct value *v)
{
  if (v && v->kind == VALUE_TYPE)
    emitname(v);
  else if (class)
    putchar(class);
  else
    fatal("type has no QBE representation");
}

static unsigned emittypeid;

/* declares a synthetic two-word aggregate type for a value this
 * compiler represents as memory (an address) but qbe has no native
 * type for, __int128 (two 'l' words) and complex (two 's' or 'd'
 * words) so far. once qbe sees the declaration, its own aggregate-type
 * abi lowering classifies parameters and return values per the real
 * system v abi (register pair or stack/memory as the fields dictate)
 * exactly the way it already does for a real two-word struct, so
 * neither caller needs any abi classification code of its own */
static void
emitpairtype(struct type *t, const char *name, char class)
{
  t->value         = xmalloc(sizeof(*t->value));
  t->value->kind   = VALUE_TYPE;
  t->value->u.name = name;
  t->value->id     = ++emittypeid;
  fputs("type ", stdout);
  emitname(t->value);
  printf(" = { %c, %c }\n", class, class);
}

/* XXX: need to consider _alignas on struct members */
static void
emittype(struct type *t)
{
  struct member     *m, *other;
  struct type       *sub;
  unsigned long long off;

  if (t->value
      || t->kind != TYPESTRUCT && t->kind != TYPEUNION && t->kind != TYPEINT128
             && t->kind != TYPECOMPLEX)
    return;
  if (t->kind == TYPEINT128) {
    emitpairtype(t, t->u.arith.issigned ? "i128" : "u128", 'l');
    return;
  }
  if (t->kind == TYPECOMPLEX) {
    emitpairtype(t, t->base == &typefloat ? "cfloat" : "cdouble", qbetype(t->base).base);
    return;
  }
  t->value         = xmalloc(sizeof(*t->value));
  t->value->kind   = VALUE_TYPE;
  t->value->u.name = t->u.structunion.tag;
  t->value->id     = ++emittypeid;
  for (m = t->u.structunion.members; m; m = m->next) {
    for (sub = m->type; sub->kind == TYPEARRAY; sub = sub->base)
      ;
    emittype(sub);
  }
  fputs("type ", stdout);
  emitname(t->value);
  if (t == targ->typevalist) {
    printf(" = align %d { %llu }\n", t->align, t->size);
    return;
  }
  fputs(" = { ", stdout);
  for (m = t->u.structunion.members, off = 0; m;) {
    if (t->kind == TYPESTRUCT) {
      /* look for a subsequent member with a larger storage
 * unit */
      for (other = m->next; other; other = other->next) {
        if (other->offset >= ALIGNUP(m->offset + 1, 8))
          break;
        if (other->offset <= m->offset)
          m = other;
      }
      off = m->offset + m->type->size;
    } else {
      fputs("{ ", stdout);
    }
    for (sub = m->type; sub->kind == TYPEARRAY; sub = sub->base)
      ;
    emitclass(qbetype(sub).data, sub->value);
    if (m->type->size > sub->size)
      printf(" %llu", m->type->size / sub->size);
    if (t->kind == TYPESTRUCT) {
      fputs(", ", stdout);
      /* skip subsequent members contained within the same
 * storage unit */
      do
        m = m->next;
      while (m && m->offset < off);
    } else {
      fputs(" } ", stdout);
      m = m->next;
    }
  }
  puts("}");
}

static struct inst **
emitinst(struct inst **instp, struct inst **instend)
{
  int          op, first;
  struct inst *inst = *instp;

  putchar('\t');
  assert(inst->kind < countof(instname));
  if (inst->res.kind) {
    emitvalue(&inst->res);
    fputs(" =", stdout);
    emitclass(inst->class, inst->arg[1]);
    putchar(' ');
  }
  fputs(instname[inst->kind], stdout);
  putchar(' ');
  emitvalue(inst->arg[0]);
  ++instp;
  op = inst->kind;
  switch (op) {
    case ICALL:
      putchar('(');
      for (first = 1; instp != instend; ++instp) {
        inst = *instp;
        if (inst->kind == IVARARG) {
          fputs(", ...", stdout);
          continue;
        }
        if (inst->kind != IARG)
          break;
        if (first)
          first = 0;
        else
          fputs(", ", stdout);
        emitclass(inst->class, inst->arg[1]);
        putchar(' ');
        emitvalue(inst->arg[0]);
      }
      putchar(')');
      break;
    default:
      if (inst->arg[1]) {
        fputs(", ", stdout);
        emitvalue(inst->arg[1]);
      }
  }
  putchar('\n');
  return instp;
}

static void
emitjump(struct jump *j)
{
  switch (j->kind) {
    case JUMP_NONE:
      break;
    case JUMP_RET:
      fputs("\tret", stdout);
      if (j->arg) {
        fputc(' ', stdout);
        emitvalue(j->arg);
      }
      putchar('\n');
      break;
    case JUMP_JMP:
      fputs("\tjmp ", stdout);
      emitname(&j->blk[0]->label);
      putchar('\n');
      break;
    case JUMP_JNZ:
      fputs("\tjnz ", stdout);
      emitvalue(j->arg);
      fputs(", ", stdout);
      emitname(&j->blk[0]->label);
      fputs(", ", stdout);
      emitname(&j->blk[1]->label);
      putchar('\n');
      break;
    case JUMP_HLT:
      fputs("\thlt\n", stdout);
      break;
    default:
      assert(0);
  }
}

void
emitfunc(struct func *f, bool global)
{
  struct block *b;
  struct inst **inst, **instend;
  struct decl  *p;
  struct value *v;

  if (f->end->jump.kind == JUMP_NONE) {
    v = NULL;
    /* implicitly return 0 from main if we reach the end of the
 * function */
    if (strcmp(f->name, "main") == 0 && f->type->base == &typeint)
      v = mkintconst(0);
    funcret(f, v);
  }
  if (global)
    puts("export");
  fputs("function ", stdout);
  if (f->type->base != &typevoid) {
    emitclass(qbetype(f->type->base).base, f->type->base->value);
    putchar(' ');
  }
  emitname(f->decl->value);
  putchar('(');
  for (p = f->type->u.func.params, v = f->paramtemps; p; p = p->next, ++v) {
    if (p != f->type->u.func.params)
      fputs(", ", stdout);
    emitclass(qbetype(p->type).base, p->type->value);
    putchar(' ');
    emitname(v);
  }
  if (f->type->u.func.isvararg) {
    if (f->type->u.func.params)
      fputs(", ", stdout);
    fputs("...", stdout);
  }
  puts(") {");
  for (b = f->start; b; b = b->next) {
    emitname(&b->label);
    putchar('\n');
    if (b->phi.res.kind) {
      putchar('\t');
      emitvalue(&b->phi.res);
      printf(" =%c phi ", b->phi.class);
      emitname(&b->phi.blk[0]->label);
      putchar(' ');
      emitvalue(b->phi.val[0]);
      fputs(", ", stdout);
      emitname(&b->phi.blk[1]->label);
      putchar(' ');
      emitvalue(b->phi.val[1]);
      putchar('\n');
    }
    instend = (struct inst **)((char *)b->insts.val + b->insts.len);
    for (inst = b->insts.val; inst != instend;)
      inst = emitinst(inst, instend);
    emitjump(&b->jump);
  }
  puts("}");
}

static void
dataitem(struct expr *expr, unsigned long long size)
{
  struct decl *decl;
  size_t       i, w;
  unsigned     c;

  switch (expr->kind) {
    case EXPRUNARY:
      if (expr->op != TBAND)
        fatal("not a address expr");
      expr = expr->base;
      if (expr->kind != EXPRIDENT)
        error(&tok.loc, "initializer is not a constant expression");
      decl = expr->u.ident.decl;
      if (decl->kind == DECLOBJECT && decl->u.obj.storage != SDSTATIC)
        error(&tok.loc, "initializer is not a constant expression");
      emitname(decl->value);
      break;
    case EXPRBINARY:
      if (expr->op != TADD || expr->u.binary.l->kind != EXPRUNARY
          || expr->u.binary.r->kind != EXPRCONST)
        error(&tok.loc, "initializer is not a constant expression");
      dataitem(expr->u.binary.l, 0);
      fputs(" + ", stdout);
      dataitem(expr->u.binary.r, 0);
      break;
    case EXPRCONST:
      if (expr->type->prop & PROPFLOAT)
        printf("%c_%.17g", expr->type->size == 4 ? 's' : 'd', expr->u.constant.f);
      else
        printf("%llu", expr->u.constant.u);
      break;
    case EXPRSTRING:
      w = expr->type->base->size;
      if (w == 1) {
        fputc('"', stdout);
        for (i = 0; i < expr->u.string.size && i < size; ++i) {
          c = ((unsigned char *)expr->u.string.data)[i];
          if (isprint(c) && c != '"' && c != '\\')
            putchar(c);
          else
            printf("\\%03o", c);
        }
        fputc('"', stdout);
      } else {
        for (i = 0; i < expr->u.string.size && i * w < size; ++i) {
          switch (w) {
            case 2:
              printf("%" PRIuLEAST16 " ", ((uint_least16_t *)expr->u.string.data)[i]);
              break;
            case 4:
              printf("%" PRIuLEAST32 " ", ((uint_least32_t *)expr->u.string.data)[i]);
              break;
            default:
              assert(0);
          }
        }
      }
      if (i * w < size)
        printf(", z %llu", size - (unsigned long long)i * w);
      break;
    default:
      error(&tok.loc, "initializer is not a constant expression");
  }
}

void
emitdata(struct decl *d, struct init *init)
{
  struct init       *cur;
  struct type       *t;
  unsigned long long offset = 0, start, end, bits = 0;
  size_t             i;
  int                align;

  align = d->u.obj.align;
  for (cur = init; cur; cur = cur->next)
    cur->expr = eval(cur->expr);
  if (d->u.obj.storage == SDTHREAD)
    fputs("thread ", stdout);
  if (d->linkage == LINKEXTERN)
    fputs("export ", stdout);
  fputs("data ", stdout);
  emitname(d->value);
  printf(" = align %d { ", align);

  while (init) {
    cur = init;
    while ((init = init->next)
           && init->start * 8 + init->bits.before < cur->end * 8 - cur->bits.after) {
      /*
 * XXX: currently, if multiple union members are
 * initialized, these assertions may not hold
      (https:// todo.sr.ht/~mcf/cproc/38)
 */
      assert(cur->expr->kind == EXPRSTRING);
      assert(init->expr->kind == EXPRCONST);
      i = (init->start - cur->start) / cur->expr->type->base->size;
      switch (cur->expr->type->base->size) {
        case 1:
          ((unsigned char *)cur->expr->u.string.data)[i] = init->expr->u.constant.u;
          break;
        case 2:
          ((uint_least16_t *)cur->expr->u.string.data)[i] = init->expr->u.constant.u;
          break;
        case 4:
          ((uint_least32_t *)cur->expr->u.string.data)[i] = init->expr->u.constant.u;
          break;
      }
    }
    start = cur->start + cur->bits.before / 8;
    end   = cur->end - (cur->bits.after + 7) / 8;
    if (offset < start && bits) {
      printf("b %u, ", (unsigned)bits); /* unfinished byte from previous
 * bit-field */
      ++offset;
      bits = 0;
    }
    if (offset < start)
      printf("z %llu, ", start - offset);
    if (cur->bits.before || cur->bits.after) {
      /* little-endian target specific */
      assert(cur->expr->type->prop & PROPINT);
      assert(cur->expr->kind == EXPRCONST);
      bits |= cur->expr->u.constant.u << cur->bits.before % 8;
      for (offset = start; offset < end; ++offset, bits >>= 8)
        printf("b %u, ", (unsigned)bits & 0xff);
      /*
 * clear the upper `after` bits in the last byte,
 * or all bits when `after` is 0 (we ended on a
 * byte boundary)
 */
      bits &= 0x7f >> (cur->bits.after + 7) % 8;
    } else {
      t = cur->expr->type;
      if (t->kind == TYPEARRAY)
        t = t->base;
      printf("%c ", qbetype(t).data);
      dataitem(cur->expr, cur->end - cur->start);
      fputs(", ", stdout);
    }
    offset = end;
  }
  if (bits) {
    printf("b %u, ", (unsigned)bits);
    ++offset;
  }
  assert(offset <= d->type->size);
  if (offset < d->type->size)
    printf("z %llu ", d->type->size - offset);
  puts("}");
}
