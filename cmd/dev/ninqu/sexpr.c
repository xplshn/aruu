#include "ninqu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* track nesting so $(EXTRA_LIBS:$(BASESTEM)) does not stop at the
 * inner close paren. shared by kv_expand and subst_tmpl */
const char *
find_dollar_close(const char *inner)
{
  const char *scan  = inner;
  int         depth = 1;

  while (*scan) {
    if (scan[0] == '$' && scan[1] == '(') {
      depth++;
      scan += 2;
      continue;
    }
    if (*scan == ')') {
      if (--depth == 0)
        return scan;
    }
    scan++;
  }
  return NULL;
}

struct SNode *
snode_atom(const char *s, int line)
{
  struct SNode *n = ecalloc(1, sizeof *n);
  n->kind         = S_ATOM;
  n->atom         = estrdup(s);
  n->line         = line;
  return n;
}

struct SNode *
snode_list(int line)
{
  struct SNode *n = ecalloc(1, sizeof *n);
  n->kind         = S_LIST;
  n->line         = line;
  return n;
}

void
snode_push(struct SNode *list, struct SNode *kid)
{
  if (list->nkids == 0) {
    list->kids = emalloc(8 * sizeof *list->kids);
  } else {
    int cap = 8;
    while (cap < list->nkids)
      cap *= 2;
    if (list->nkids == cap)
      list->kids = erealloc(list->kids, (size_t)cap * 2 * sizeof *list->kids);
  }
  list->kids[list->nkids++] = kid;
}

void
snode_free(struct SNode *n)
{
  int i;
  if (!n)
    return;
  if (n->kind == S_LIST) {
    for (i = 0; i < n->nkids; i++)
      snode_free(n->kids[i]);
    free(n->kids);
  }
  free(n->atom);
  free(n);
}

/* deep copy. a template body is parsed from a file that gets freed
 * once load_manifest returns, so the stored copy cannot point back
 * into that tree */
struct SNode *
snode_clone(struct SNode *n)
{
  struct SNode *out;
  int           i;

  if (!n)
    return NULL;
  if (n->kind == S_ATOM)
    return snode_atom(n->atom, n->line);
  out = snode_list(n->line);
  for (i = 0; i < n->nkids; i++)
    snode_push(out, snode_clone(n->kids[i]));
  return out;
}

/* replace each $(PARAM) with its argument text. any other $(...) is
 * copied verbatim for the later kv_expand pass, so a template body
 * can reference $(BASESTEM) the same way a hand-written rule does */
char *
subst_tmpl(const char *s, struct StrList *params, char **vals)
{
  size_t      cap = strlen(s) * 2 + 64, len = 0;
  char       *out = emalloc(cap);
  const char *p   = s;

  while (*p) {
    if (p[0] == '$' && p[1] == '(') {
      const char *inner = p + 2;
      const char *close = find_dollar_close(inner);

      if (close) {
        char   name[256];
        size_t nl = (size_t)(close - inner);
        size_t tokl;
        int    pi;

        if (nl >= sizeof name)
          nl = sizeof name - 1;
        memcpy(name, inner, nl);
        name[nl] = '\0';

        for (pi = 0; pi < params->n; pi++)
          if (strcmp(params->v[pi], name) == 0)
            break;

        tokl = (pi < params->n) ? strlen(vals[pi]) : (size_t)(close - p) + 1;
        while (len + tokl + 1 >= cap) {
          cap *= 2;
          out = erealloc(out, cap);
        }
        memcpy(out + len, pi < params->n ? vals[pi] : p, tokl);
        len += tokl;
        p = close + 1;
        continue;
      }
    }
    if (len + 2 >= cap) {
      cap *= 2;
      out = erealloc(out, cap);
    }
    out[len++] = *p++;
  }
  out[len] = '\0';
  return out;
}

void
snode_subst(struct SNode *n, struct StrList *params, char **vals)
{
  int i;
  if (!n)
    return;
  if (n->kind == S_ATOM) {
    char *out = subst_tmpl(n->atom, params, vals);
    free(n->atom);
    n->atom = out;
    return;
  }
  for (i = 0; i < n->nkids; i++)
    snode_subst(n->kids[i], params, vals);
}

/* swallow a balanced $(...) inside a bare atom */
void
lex_swallow_dollar(struct SLex *lx, char *buf, size_t *blen, size_t bsz)
{
  int depth = 1;

  if (*blen + 2 < bsz) {
    buf[(*blen)++] = '$';
    buf[(*blen)++] = '(';
  }
  lx->pos += 2;
  while (depth > 0) {
    int e = lx->src[lx->pos];
    if (e == '\0')
      eprintf("manifest: unterminated $(...) on line %d\n", lx->line);
    if (e == '\n')
      lx->line++;
    if (e == '$' && lx->src[lx->pos + 1] == '(') {
      depth++;
      if (*blen + 2 < bsz) {
        buf[(*blen)++] = e;
        buf[(*blen)++] = lx->src[lx->pos + 1];
      }
      lx->pos += 2;
      continue;
    }
    if (*blen < bsz - 1)
      buf[(*blen)++] = e;
    if (e == ')')
      depth--;
    lx->pos++;
  }
}

/* \" \\ \n \t \r \$ are recognized. anything else keeps the backslash
 * so shell escapes like \( survive */
void
lex_string(struct SLex *lx, char *buf, size_t bsz)
{
  int    line = lx->line;
  size_t blen = 0;

  lx->pos++;
  while (lx->src[lx->pos] && lx->src[lx->pos] != '"') {
    int e = lx->src[lx->pos];

    if (e == '\\' && lx->src[lx->pos + 1]) {
      int esc = lx->src[lx->pos + 1];
      switch (esc) {
        case 'n':
          e = '\n';
          break;
        case 't':
          e = '\t';
          break;
        case 'r':
          e = '\r';
          break;
        case '"':
        case '\\':
        case '$':
          e = esc;
          break;
        default:
          if (blen < bsz - 1)
            buf[blen++] = '\\';
          e = esc;
          break;
      }
      lx->pos += 2;
    } else {
      lx->pos++;
    }
    if (e == '\n')
      lx->line++;
    if (blen < bsz - 1)
      buf[blen++] = (char)e;
  }
  if (lx->src[lx->pos] != '"')
    eprintf("manifest: unterminated string on line %d\n", line);
  lx->pos++;
  buf[blen] = '\0';
}

struct SNode *
snode_parse(struct SLex *lx)
{
  char           buf[8192];
  size_t         blen;
  int            line;
  struct SNode  *root  = snode_list(lx->line);
  struct SNode **stack = emalloc(16 * sizeof *stack);
  int            sp = 0, scap = 16;

  stack[sp++] = root;

  for (;;) {
    int c = lx->src[lx->pos];

    if (c == '\0')
      break;
    if (c == '\n') {
      lx->line++;
      lx->pos++;
      continue;
    }
    if (isspace((unsigned char)c)) {
      lx->pos++;
      continue;
    }
    if (c == ';') {
      while (lx->src[lx->pos] && lx->src[lx->pos] != '\n')
        lx->pos++;
      continue;
    }
    if (c == '(') {
      struct SNode *lst = snode_list(lx->line);
      snode_push(stack[sp - 1], lst);
      if (sp >= scap) {
        scap *= 2;
        stack = erealloc(stack, (size_t)scap * sizeof *stack);
      }
      stack[sp++] = lst;
      lx->pos++;
      continue;
    }
    if (c == ')') {
      if (sp <= 1)
        eprintf("manifest: stray ')' on line %d\n", lx->line);
      sp--;
      lx->pos++;
      continue;
    }
    if (c == '"') {
      line = lx->line;
      lex_string(lx, buf, sizeof buf);
      snode_push(stack[sp - 1], snode_atom(buf, line));
      continue;
    }

    /* bare atom */
    line = lx->line;
    blen = 0;
    while (lx->src[lx->pos]) {
      int d = lx->src[lx->pos];
      if (d == '(' || d == ')' || d == '"' || d == ';' || isspace((unsigned char)d))
        break;
      if (d == '$' && lx->src[lx->pos + 1] == '(') {
        lex_swallow_dollar(lx, buf, &blen, sizeof buf);
        continue;
      }
      if (blen < sizeof buf - 1)
        buf[blen++] = d;
      lx->pos++;
    }
    buf[blen] = '\0';
    snode_push(stack[sp - 1], snode_atom(buf, line));
  }

  if (sp != 1)
    eprintf("manifest: %d unclosed '(' at end of file\n", sp - 1);

  free(stack);
  return root;
}

int
s_is_list(struct SNode *n)
{
  return n && n->kind == S_LIST;
}

const char *
s_head(struct SNode *n)
{
  if (!s_is_list(n) || n->nkids == 0 || n->kids[0]->kind != S_ATOM)
    return NULL;
  return n->kids[0]->atom;
}
