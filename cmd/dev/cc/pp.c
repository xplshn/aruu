#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"
#include "cc.h"

struct macroparam {
  char *name;
  enum {
    PARAMTOK = 1 << 0, /* the parameter is used normally */
    PARAMSTR = 1 << 1, /* the parameter is used with the '#' operator */
    PARAMVAR = 1 << 2, /* the parameter is __VA_ARGS__ */
  } flags;
};

struct macroarg {
  struct token *token;
  size_t        ntoken;
  /* stringized argument */
  struct token str;
};

struct macro {
  enum {
    MACROOBJ,
    MACROFUNC,
  } kind;
  char *name;
  /* whether or not this macro is ineligible for expansion */
  bool hide;
  /* parameters of function-like macro */
  struct macroparam *param;
  size_t             nparam;
  /* argument tokens of macro invocation */
  struct macroarg *arg;
  /* replacement list */
  struct token *token;
  size_t        ntoken;
};

struct frame {
  struct token *token;
  size_t        ntoken;
  struct macro *macro;
  struct token *alloc;
  bool          is_substituted;
};

enum ppflags ppflags;

static struct array ctx;
static struct array macros;
/* number of macros currently undergoing expansion */
static size_t        macrodepth;
static struct macro *macroget(enum tokenkind);

static bool disexpand;
static int  cppoff;
static struct condstate {
  int enabled;
  int done;
  int iselse;
} condstack[64];
static int conddepth;

static const char **incdirs;
static size_t       nincdirs;

void
addincdir(const char *dir)
{
  incdirs             = xreallocarray(incdirs, nincdirs + 1, sizeof(*incdirs));
  incdirs[nincdirs++] = dir;
}

static char *
strdup_internal(const char *s)
{
  size_t len = strlen(s);
  char  *d   = xmalloc(len + 1);
  memcpy(d, s, len + 1);
  return d;
}

static void
include(void)
{
  bool  is_system;
  char *name = scaninclude(&is_system);
  if (!name) {
    error(&tok.loc, "expected filename after #include");
    return;
  }

  char  path[FILENAME_MAX];
  FILE *f = NULL;

  if (!is_system) {
    const char *curfile = tok.loc.file;
    const char *slash   = strrchr(curfile, '/');
    if (slash) {
      size_t len = slash - curfile;
      if (len + 1 + strlen(name) < sizeof(path)) {
        memcpy(path, curfile, len);
        path[len] = '/';
        strcpy(path + len + 1, name);
        f = fopen(path, "r");
      }
    } else {
      if (strlen(name) < sizeof(path)) {
        strcpy(path, name);
        f = fopen(path, "r");
      }
    }
  }

  if (!f) {
    for (size_t i = 0; i < nincdirs; i++) {
      if (strlen(incdirs[i]) + 1 + strlen(name) < sizeof(path)) {
        sprintf(path, "%s/%s", incdirs[i], name);
        f = fopen(path, "r");
        if (f)
          break;
      }
    }
  }

  /* freestanding headers (stddef.h etc) ship in an include/ dir next to
     the compiler binary itself, the same layout gcc and clang use for
     their own built-in headers */
  if (!f) {
    char    self[FILENAME_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n == -1) {
      n = strlen(argv0);
      if (n > (ssize_t)sizeof(self) - 1)
        n = sizeof(self) - 1;
      memcpy(self, argv0, n);
    }
    self[n] = '\0';

    char *dir = strrchr(self, '/');
    if (dir) {
      *dir = '\0';
      if (strlen(self) + sizeof("/include/") + strlen(name) < sizeof(path)) {
        snprintf(path, sizeof(path), "%s/include/%s", self, name);
        f = fopen(path, "r");
      }
    }
  }

  if (!f) {
    error(&tok.loc, "included file '%s' not found", name);
    free(name);
    return;
  }

  while (tok.kind != TNEWLINE && tok.kind != TEOF)
    scan(&tok);

  scanfrom(strdup_internal(path), f);
  free(name);
}

struct PPVal {
  enum tokenkind kind;
  long long      val;
};

static struct PPVal *ppvals;
static size_t        nppvals, capppvals;
static size_t        validx;

static void
addval(enum tokenkind kind, long long val)
{
  if (nppvals >= capppvals) {
    capppvals = capppvals ? capppvals * 2 : 16;
    ppvals    = xreallocarray(ppvals, capppvals, sizeof(*ppvals));
  }
  ppvals[nppvals].kind = kind;
  ppvals[nppvals].val  = val;
  nppvals++;
}

static long long eval_cond_expr(void);

static long long
eval_primary(void)
{
  if (validx >= nppvals)
    return 0;
  struct PPVal p = ppvals[validx++];
  if (p.kind == TNUMBER) {
    return p.val;
  }
  if (p.kind == TLPAREN) {
    long long val = eval_cond_expr();
    if (validx < nppvals && ppvals[validx].kind == TRPAREN) {
      validx++;
    }
    return val;
  }
  if (p.kind == TADD) {
    return eval_primary();
  }
  if (p.kind == TSUB) {
    return -eval_primary();
  }
  if (p.kind == TLNOT) {
    return !eval_primary();
  }
  if (p.kind == TBNOT) {
    return ~eval_primary();
  }
  return 0;
}

static long long
eval_mul(void)
{
  long long val = eval_primary();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TMUL || op == TDIV || op == TMOD) {
      validx++;
      long long rhs = eval_primary();
      if (op == TMUL)
        val *= rhs;
      else if (op == TDIV)
        val = rhs ? val / rhs : 0;
      else
        val = rhs ? val % rhs : 0;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_add(void)
{
  long long val = eval_mul();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TADD || op == TSUB) {
      validx++;
      long long rhs = eval_mul();
      if (op == TADD)
        val += rhs;
      else
        val -= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_shift(void)
{
  long long val = eval_add();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TSHL || op == TSHR) {
      validx++;
      long long rhs = eval_add();
      if (op == TSHL)
        val <<= rhs;
      else
        val >>= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_rel(void)
{
  long long val = eval_shift();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TLESS || op == TGREATER || op == TLEQ || op == TGEQ) {
      validx++;
      long long rhs = eval_shift();
      if (op == TLESS)
        val = val < rhs;
      else if (op == TGREATER)
        val = val > rhs;
      else if (op == TLEQ)
        val = val <= rhs;
      else
        val = val >= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_eq(void)
{
  long long val = eval_rel();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TEQL || op == TNEQ) {
      validx++;
      long long rhs = eval_rel();
      if (op == TEQL)
        val = val == rhs;
      else
        val = val != rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_band(void)
{
  long long val = eval_eq();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TBAND) {
      validx++;
      long long rhs = eval_eq();
      val &= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_bxor(void)
{
  long long val = eval_band();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TXOR) {
      validx++;
      long long rhs = eval_band();
      val ^= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_bor(void)
{
  long long val = eval_bxor();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TBOR) {
      validx++;
      long long rhs = eval_bxor();
      val |= rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_and(void)
{
  long long val = eval_bor();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TLAND) {
      validx++;
      long long rhs = eval_bor();
      val           = val && rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_or(void)
{
  long long val = eval_and();
  while (validx < nppvals) {
    enum tokenkind op = ppvals[validx].kind;
    if (op == TLOR) {
      validx++;
      long long rhs = eval_and();
      val           = val || rhs;
    } else {
      break;
    }
  }
  return val;
}

static long long
eval_cond_expr(void)
{
  long long val = eval_or();
  if (validx < nppvals && ppvals[validx].kind == TQUESTION) {
    validx++;
    long long true_val = eval_cond_expr();
    if (validx < nppvals && ppvals[validx].kind == TCOLON) {
      validx++;
    }
    long long false_val = eval_cond_expr();
    val                 = val ? true_val : false_val;
  }
  return val;
}

static long long
eval_if_expr(void)
{
  enum ppflags old_flags  = ppflags;
  int          old_cppoff = cppoff;
  cppoff                  = 0;
  ppflags |= PPNEWLINE;
  nppvals = 0;
  next();
  while (tok.kind != TNEWLINE && tok.kind != TEOF) {
    if (tok.kind >= TPPIDENT && strcmp(tokenstr(tok.kind), "defined") == 0) {
      bool paren = false;
      disexpand  = true;
      next();
      if (tok.kind == TLPAREN) {
        paren = true;
        next();
      }
      if (tok.kind < TPPIDENT) {
        error(
            &tok.loc,
            "operator 'defined' requires "
            "an identifier"
        );
      }
      struct macro *m   = macroget(tok.kind);
      long long     res = (m != NULL);
      if (paren) {
        next();
        if (tok.kind != TRPAREN)
          error(&tok.loc, "expected ')' in defined()");
      }
      disexpand = false;
      addval(TNUMBER, res);
      next();
    } else if (tok.kind >= TPPIDENT) {
      addval(TNUMBER, 0);
      next();
    } else if (tok.kind == TNUMBER) {
      long long v = strtoull(tok.lit, NULL, 0);
      addval(TNUMBER, v);
      next();
    } else {
      addval(tok.kind, 0);
      next();
    }
  }
  cppoff  = old_cppoff;
  ppflags = old_flags;
  validx  = 0;
  return eval_cond_expr();
}

void
ppinit(void)
{
  next();
}

static void
macrodel(struct macro *m)
{
  free(m->param);
  free(m->token);
  free(m);
}

/* check if two macro definitions are equal, as in C11 6.10.3p2 */
static bool
macroequal(struct macro *m1, struct macro *m2)
{
  struct macroparam *p1, *p2;
  struct token      *t1, *t2;

  if (m1->kind != m2->kind)
    return false;
  if (m1->kind == MACROFUNC) {
    if (m1->nparam != m2->nparam)
      return false;
    for (p1 = m1->param, p2 = m2->param; p1 < m1->param + m1->nparam; ++p1, ++p2) {
      if (strcmp(p1->name, p2->name) != 0 || p1->flags != p2->flags)
        return false;
    }
  }
  if (m1->ntoken != m2->ntoken)
    return false;
  for (t1 = m1->token, t2 = m2->token; t1 < m1->token + m1->ntoken; ++t1, ++t2) {
    if (t1->kind != t2->kind)
      return false;
    if (t1->lit && strcmp(t1->lit, t2->lit) != 0)
      return false;
  }
  return true;
}

/* find the index of a macro parameter with the given name */
static size_t
macroparam(struct macro *m, struct token *t)
{
  size_t i;

  if (t->kind >= TPPIDENT) {
    for (i = 0; i < m->nparam; ++i) {
      if (strcmp(m->param[i].name, tokenstr(t->kind)) == 0)
        return i;
    }
  }
  return -1;
}

/* lookup a macro by name */
static struct macro *
macroget(enum tokenkind ident)
{
  return arraygetptr(&macros, ident - TPPIDENT);
}

static void
macrodone(struct macro *m)
{
  m->hide = false;
  if (m->kind == MACROFUNC && m->nparam > 0) {
    free(m->arg[0].token);
    free(m->arg);
  }
  --macrodepth;
}

static bool
macrovarargs(struct macro *m)
{
  return m->kind == MACROFUNC && m->nparam > 0 && m->param[m->nparam - 1].flags & PARAMVAR;
}

static struct token *
framenext(struct frame *f)
{
  return f->ntoken--, f->token++;
}

/* push a new context frame */
static struct frame *
ctxpush(struct token *t, size_t n, struct macro *m, bool space)
{
  struct frame *f;

  f                 = arrayadd(&ctx, sizeof(*f));
  f->token          = t;
  f->ntoken         = n;
  f->macro          = m;
  f->alloc          = NULL;
  f->is_substituted = false;
  if (n > 0)
    t[0].space = space;
  return f;
}

static struct frame *
ctxpush_alloc(struct token *t, size_t n, struct macro *m, bool space, struct token *alloc)
{
  struct frame *f;

  f                 = arrayadd(&ctx, sizeof(*f));
  f->token          = t;
  f->ntoken         = n;
  f->macro          = m;
  f->alloc          = alloc;
  f->is_substituted = true;
  if (n > 0)
    t[0].space = space;
  return f;
}

/* get the next token from the context */
static struct token *
ctxnext(void)
{
  struct frame *f;
  struct token *t;
  struct macro *m;
  bool          space;
  size_t        i;

again:
  for (f = arraylast(&ctx, sizeof(*f)); ctx.len; --f, ctx.len -= sizeof(*f)) {
    if (f->ntoken)
      break;
    if (f->macro)
      macrodone(f->macro);
    if (f->alloc)
      free(f->alloc);
  }
  if (ctx.len == 0)
    return NULL;
  m = f->macro;
  if (m && m->kind == MACROFUNC && !f->is_substituted) {
    /* try to expand macro parameter */
    space = f->token->space;
    switch (f->token->kind) {
      case THASH:
        framenext(f);
        t = framenext(f);
        assert(t);
        i = macroparam(m, t);
        assert(i != -1);
        f = ctxpush(&m->arg[i].str, 1, NULL, space);
        break;
      default:
        if (f->token->kind >= TPPIDENT) {
          i = macroparam(m, f->token);
          if (i == -1)
            break;
          framenext(f);
          if (m->arg[i].ntoken == 0)
            goto again;
          f = ctxpush(m->arg[i].token, m->arg[i].ntoken, NULL, space);
        }
        break;
    }
    /* XXX: token concatenation */
  }
  return framenext(f);
}

static void
define(void)
{
  struct token      *t;
  enum tokenkind     ident, prev;
  struct macro      *m, *other;
  struct macroparam *p;
  struct array       params = {0}, repl = {0};
  size_t             i;

  m       = xmalloc(sizeof(*m));
  ident   = tok.kind;
  m->name = tokencheck(&tok, TPPIDENT, "after #define");
  m->hide = false;
  t       = arrayadd(&repl, sizeof(*t));
  scan(t);
  if (t->kind == TLPAREN && !t->space) {
    m->kind = MACROFUNC;
    /* read macro parameter names */
    p = NULL;
    while (scan(&tok), tok.kind != TRPAREN) {
      if (p) {
        if (p->flags & PARAMVAR)
          tokencheck(&tok, TRPAREN, "after '...'");
        tokencheck(&tok, TCOMMA, "or ')' after macro parameter");
        scan(&tok);
      }
      p        = arrayadd(&params, sizeof(*p));
      p->flags = 0;
      if (tok.kind == TELLIPSIS) {
        p->name = "__VA_ARGS__";
        p->flags |= PARAMVAR;
      } else {
        p->name = tokencheck(&tok, TPPIDENT, "of macro parameter name or '...'");
      }
    }
    scan(t); /* first token in replacement list */
  } else {
    m->kind = MACROOBJ;
  }
  m->param  = params.val;
  m->nparam = params.len / sizeof(m->param[0]);

  /* read macro body */
  i = macroparam(m, t);
  while (t->kind != TNEWLINE && t->kind != TEOF) {
    prev = t->kind;
    t    = arrayadd(&repl, sizeof(*t));
    scan(t);
    if (t->kind == T__VA_ARGS__ && !macrovarargs(m))
      error(
          &t->loc,
          "__VA_ARGS__ can only be used in "
          "variadic function-like macros"
      );
    if (m->kind != MACROFUNC)
      continue;
    if (i != -1)
      m->param[i].flags |= PARAMTOK;
    i = macroparam(m, t);
    if (prev == THASH) {
      tokencheck(t, TPPIDENT, "after '#' operator");
      if (i == -1)
        error(&t->loc, "'%s' is not a macro parameter name", t->lit);
      m->param[i].flags |= PARAMSTR;
      i = -1;
    }
  }
  m->token  = repl.val;
  m->ntoken = repl.len / sizeof(*t) - 1;
  tok       = *t;

  i     = ident - TPPIDENT;
  other = arraysetptr(&macros, i, m);
  if (other) {
    if (!macroequal(m, other))
      error(&tok.loc, "redefinition of macro '%s'", m->name);
    macrodel(other);
  }
}

static void
undef(void)
{
  enum tokenkind ident;
  struct macro  *m;

  ident = tok.kind;
  tokencheck(&tok, TPPIDENT, "after #undef");
  m = macroget(ident);
  if (m) {
    macrodel(m);
    ((void **)macros.val)[ident - TPPIDENT] = NULL;
  }
  scan(&tok);
}

static void
directive(void)
{
  struct location newloc;
  enum ppflags    oldflags;
  enum tokenkind  kind;

  scan(&tok);
  if (tok.kind == TNEWLINE)
    return; /* empty directive */
  oldflags = ppflags;
  ppflags |= PPNEWLINE;
  kind = tok.kind;
  switch (kind) {
    case TNUMBER:
      goto line; /* gcc line markers */
    case TIF: {
      int enabled = 0;
      if (!cppoff) {
        enabled = (eval_if_expr() != 0);
      } else {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
      }
      if (conddepth >= 64)
        error(&tok.loc, "too many nested #if");
      condstack[conddepth].iselse  = 0;
      condstack[conddepth].done    = enabled || cppoff;
      condstack[conddepth].enabled = enabled && !cppoff;
      if (!condstack[conddepth].enabled)
        cppoff++;
      conddepth++;
      break;
    }
    case TIFDEF:
    case TIFNDEF: {
      int enabled = 0;
      if (!cppoff) {
        scan(&tok);
        struct macro *m = macroget(tok.kind);
        enabled         = (m != NULL);
        if (kind == TIFNDEF)
          enabled = !enabled;
      }
      if (conddepth >= 64)
        error(&tok.loc, "too many nested #if");
      condstack[conddepth].iselse  = 0;
      condstack[conddepth].done    = enabled || cppoff;
      condstack[conddepth].enabled = enabled && !cppoff;
      if (!condstack[conddepth].enabled)
        cppoff++;
      conddepth++;
      while (tok.kind != TNEWLINE && tok.kind != TEOF)
        scan(&tok);
      break;
    }
    case TELIF: {
      if (conddepth == 0)
        error(&tok.loc, "#elif without #if");
      if (condstack[conddepth - 1].iselse)
        error(&tok.loc, "#elif after #else");
      int enabled = 0;
      if (cppoff == 1 && !condstack[conddepth - 1].enabled && !condstack[conddepth - 1].done) {
        enabled = (eval_if_expr() != 0);
        if (enabled) {
          condstack[conddepth - 1].enabled = 1;
          condstack[conddepth - 1].done    = 1;
          cppoff                           = 0;
        }
      } else {
        if (cppoff == 0) {
          condstack[conddepth - 1].enabled = 0;
          cppoff                           = 1;
        }
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
      }
      break;
    }
    case TELSE:
      if (conddepth == 0)
        error(&tok.loc, "#else without #if");
      if (condstack[conddepth - 1].iselse)
        error(&tok.loc, "multiple #else");
      condstack[conddepth - 1].iselse = 1;
      if (cppoff == 1 && !condstack[conddepth - 1].enabled && !condstack[conddepth - 1].done) {
        condstack[conddepth - 1].enabled = 1;
        condstack[conddepth - 1].done    = 1;
        cppoff                           = 0;
      } else if (cppoff == 0) {
        condstack[conddepth - 1].enabled = 0;
        cppoff                           = 1;
      }
      while (tok.kind != TNEWLINE && tok.kind != TEOF)
        scan(&tok);
      break;
    case TENDIF:
      if (conddepth == 0)
        error(&tok.loc, "#endif without #if");
      conddepth--;
      if (!condstack[conddepth].enabled) {
        cppoff--;
      }
      while (tok.kind != TNEWLINE && tok.kind != TEOF)
        scan(&tok);
      break;
    case TINCLUDE: {
      if (cppoff) {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
        break;
      }
      include();
      break;
    }
    case TDEFINE:
      if (cppoff) {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
        break;
      }
      scan(&tok);
      define();
      break;
    case TUNDEF:
      if (cppoff) {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
        break;
      }
      scan(&tok);
      undef();
      break;
    case TLINE:
      if (cppoff) {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
        break;
      }
      scan(&tok);
      tokencheck(&tok, TNUMBER, "after #line");
    line:
      newloc.line = strtoull(tok.lit, NULL, 0);
      newloc.col  = 1;
      scan(&tok);
      newloc.file = tok.loc.file;
      if (tok.kind == TSTRINGLIT) {
        char *quote;

        /* XXX: handle escape sequences (reuse string decoding
 * from expr.c) */
        quote = strchr(tok.lit, '"');
        assert(quote);
        newloc.file = quote + 1;
        quote       = strchr(quote + 1, '"');
        assert(quote);
        *quote = '\0';
        scan(&tok);
      }
      while (tok.kind == TNUMBER)
        scan(&tok);
      scansetloc(newloc);
      break;
    case TERROR:
    case TWARNING:
      if (cppoff) {
        while (tok.kind != TNEWLINE && tok.kind != TEOF)
          scan(&tok);
        break;
      }
      diagloc(&tok.loc);
      fprintf(stderr, "%s directive:", kind == TERROR ? "#error" : "#warning");
      scan(&tok);
      while (tok.kind != TNEWLINE && tok.kind != TEOF) {
        tokenprint(&tok, stderr);
        scan(&tok);
      }
      fputc('\n', stderr);
      if (kind == TERROR)
        exit(1);
      break;
    case TPRAGMA:
      while (tok.kind != TNEWLINE && tok.kind != TEOF)
        scan(&tok);
      break;
    default:
      error(&tok.loc, "invalid preprocessor directive #%s", tokenstr(tok.kind));
  }
  tokencheck(&tok, TNEWLINE, "after preprocessing directive");
  ppflags = oldflags;
}

/* get the next token without expanding it */
static void
nextinto(struct token *t)
{
  static bool newline = true;

  for (;;) {
    scan(t);
    if (newline && t->kind == THASH) {
      directive();
      if (cppoff) {
        newline = true;
        continue;
      }
    } else {
      newline = tok.kind == TNEWLINE;
      if (cppoff) {
        continue;
      }
      break;
    }
  }
}

static struct token *
rawnext(void)
{
  struct token *t;

  t = ctxnext();
  if (!t) {
    t = &tok;
    nextinto(t);
  }
  return t;
}

static bool
peekparen(void)
{
  static struct array pending;
  struct token       *t;
  struct frame       *f;

  t = ctxnext();
  if (t) {
    if (t->kind == TLPAREN)
      return true;
    f = arraylast(&ctx, sizeof(*f));
    --f->token;
    ++f->ntoken;
    return false;
  }
  pending.len = 0;
  do
    t = arrayadd(&pending, sizeof(*t)), nextinto(t);
  while (t->kind == TNEWLINE);
  if (t->kind == TLPAREN)
    return true;
  t = pending.val;
  ctxpush(t, pending.len / sizeof(*t), NULL, t[0].space);
  return false;
}

static void
stringize(struct array *buf, struct token *t)
{
  const char *lit;

  if ((t->space || t->kind == TNEWLINE) && buf->len > 1 && ((char *)buf->val)[buf->len - 1] != ' ')
    arrayaddbuf(buf, " ", 1);
  lit = t->lit ? t->lit : tokenstr(t->kind);
  if (t->kind == TSTRINGLIT || t->kind == TCHARCONST) {
    for (; *lit; ++lit) {
      if (*lit == '\\' || *lit == '"')
        arrayaddbuf(buf, "\\", 1);
      arrayaddbuf(buf, lit, 1);
    }
  } else if (lit) {
    arrayaddbuf(buf, lit, strlen(lit));
  }
}

static void expandfunc(struct macro *);

static void
macroexpand(struct macro *m, bool space)
{
  struct array  repl = {0};
  size_t        i, j;
  struct token *t;
  int           param_idx;

  for (i = 0; i < m->ntoken; ++i) {
    t = &m->token[i];
    if (t->kind == THASH && m->kind == MACROFUNC && i + 1 < m->ntoken) {
      struct token stringized;
      struct array str = {0};
      t                = &m->token[++i];
      param_idx        = macroparam(m, t);
      assert(param_idx != -1);
      arrayaddbuf(&str, "\"", 1);
      for (j = 0; j < m->arg[param_idx].ntoken; ++j)
        stringize(&str, &m->arg[param_idx].token[j]);
      arrayaddbuf(&str, "\"", 2);
      stringized = (struct token){
          .kind  = TSTRINGLIT,
          .lit   = str.val,
          .space = t->space,
      };
      arrayaddbuf(&repl, &stringized, sizeof(stringized));
    } else if (m->kind == MACROFUNC && (param_idx = macroparam(m, t)) != -1) {
      struct macroarg *arg = &m->arg[param_idx];
      for (j = 0; j < arg->ntoken; ++j) {
        struct token arg_t = arg->token[j];
        if (j == 0)
          arg_t.space = t->space;
        arrayaddbuf(&repl, &arg_t, sizeof(arg_t));
      }
    } else {
      arrayaddbuf(&repl, t, sizeof(*t));
    }
  }

  struct array clean = {0};
  t                  = repl.val;
  size_t n           = repl.len / sizeof(*t);
  for (i = 0; i < n; ++i) {
    if (i + 1 < n && t[i + 1].kind == THASHHASH) {
      struct token pasted = t[i];
      if (i + 2 < n) {
        struct token *right = &t[i + 2];
        char         *s1    = pasted.lit ? pasted.lit : tokenstr(pasted.kind);
        char         *s2    = right->lit ? right->lit : tokenstr(right->kind);
        size_t        len1  = strlen(s1);
        size_t        len2  = strlen(s2);
        char         *s     = xmalloc(len1 + len2 + 1);
        memcpy(s, s1, len1);
        memcpy(s + len1, s2, len2);
        s[len1 + len2] = '\0';

        pasted.lit = s;
        if (s[0] >= '0' && s[0] <= '9') {
          pasted.kind = TNUMBER;
        } else {
          pasted.kind = tokenget(s, len1 + len2);
        }
        i += 2;
      } else {
        i += 1;
      }
      t[i] = pasted;
      --i;
    } else if (t[i].kind != THASHHASH) {
      arrayaddbuf(&clean, &t[i], sizeof(t[i]));
    }
  }
  free(repl.val);

  struct token *allocated = clean.val;
  size_t        n_clean   = clean.len / sizeof(*allocated);
  ctxpush_alloc(allocated, n_clean, m, space, allocated);
}

static bool
expand(struct token *t)
{
  struct macro *m;
  bool          space;

  if (disexpand)
    return false;

  if (t->kind < TPPIDENT)
    return false;
  m = macroget(t->kind);
  if (!m || m->hide)
    t->hide = true;
  if (t->hide)
    return false;
  space = t->space;
  if (m->kind == MACROFUNC) {
    if (!peekparen())
      return false;
    expandfunc(m);
  }
  macroexpand(m, space);
  m->hide = true;
  ++macrodepth;
  return true;
}

static void
expandfunc(struct macro *m)
{
  struct macroparam *p;
  struct macroarg   *arg;
  struct array       str, tok;
  size_t             i, depth, paren;
  struct token      *t;

  /* read macro arguments */
  paren = 0;
  depth = macrodepth;
  tok   = (struct array){0};
  arg   = xreallocarray(NULL, m->nparam, sizeof(*arg));
  t     = rawnext();
  for (i = 0; i < m->nparam; ++i) {
    p = &m->param[i];
    if (p->flags & PARAMSTR) {
      str = (struct array){0};
      arrayaddbuf(&str, "\"", 1);
    }
    arg[i].ntoken = 0;
    for (;;) {
      if (t->kind == TEOF)
        error(&t->loc, "EOF when reading macro parameters");
      if (macrodepth <= depth) {
        /* adjust current macro depth, in case it got
 * shallower */
        depth = macrodepth;
        if (paren == 0 && (t->kind == TRPAREN || t->kind == TCOMMA && !(p->flags & PARAMVAR)))
          break;
        switch (t->kind) {
          case TLPAREN:
            ++paren;
            break;
          case TRPAREN:
            --paren;
            break;
        }
        if (p->flags & PARAMSTR)
          stringize(&str, t);
      }
      if (p->flags & PARAMTOK && !expand(t)) {
        arrayaddbuf(&tok, t, sizeof(*t));
        ++arg[i].ntoken;
      }
      t = rawnext();
    }
    if (p->flags & PARAMSTR) {
      arrayaddbuf(&str, "\"", 2);
      arg[i].str = (struct token){
          .kind = TSTRINGLIT,
          .lit  = str.val,
      };
    }
    if (t->kind == TRPAREN)
      break;
    t = rawnext();
  }
  if (i + 1 < m->nparam)
    error(&t->loc, "not enough arguments for macro '%s'", m->name);
  if (t->kind != TRPAREN)
    error(&t->loc, "too many arguments for macro '%s'", m->name);
  for (i = 0, t = tok.val; i < m->nparam; ++i) {
    arg[i].token = t;
    t += arg[i].ntoken;
  }
  m->arg = arg;
}

void
next(void)
{
  struct token *t;

  do
    t = rawnext();
  while (expand(t) || t->kind == TNEWLINE && !(ppflags & PPNEWLINE));
  tok = *t;

  /* keyword aliases */
  switch (tok.kind) {
    case ALIAS_ALIGNAS:
      tok.kind = TALIGNAS;
      break;
    case ALIAS_ALIGNOF:
      tok.kind = TALIGNOF;
      break;
    case ALIAS_BOOL:
      tok.kind = TBOOL;
      break;
    case ALIAS__INLINE:
    case ALIAS__INLINE__:
      tok.kind = TINLINE;
      break;
    case ALIAS__SIGNED:
    case ALIAS__SIGNED__:
      tok.kind = TSIGNED;
      break;
    case ALIAS_STATIC_ASSERT:
      tok.kind = TSTATIC_ASSERT;
      break;
    case ALIAS_THREAD_LOCAL:
    case ALIAS__THREAD:
      tok.kind = TTHREAD_LOCAL;
      break;
    case ALIAS__TYPEOF:
    case ALIAS__TYPEOF__:
      tok.kind = TTYPEOF;
      break;
    case ALIAS__VOLATILE__:
      tok.kind = TVOLATILE;
      break;
    case ALIAS__ASM:
      tok.kind = T__ASM__;
      break;
  }
}

bool
peek(enum tokenkind kind)
{
  static struct token pending;
  struct token        old;

  old = tok;
  next();
  if (tok.kind == kind) {
    next();
    return true;
  }
  pending = tok;
  tok     = old;
  ctxpush(&pending, 1, NULL, pending.space);
  return false;
}

char *
expect(enum tokenkind kind, const char *msg)
{
  char *lit;

  lit = tokencheck(&tok, kind, msg);
  next();

  return lit;
}

bool
consume(enum tokenkind kind)
{
  if (tok.kind != kind)
    return false;
  next();
  return true;
}
