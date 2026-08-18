#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Gate *
gate_new(enum GKind kind)
{
  struct Gate *g = ecalloc(1, sizeof *g);
  g->kind        = kind;
  return g;
}

void
gate_free(struct Gate *g)
{
  int i;
  if (!g)
    return;
  for (i = 0; i < g->nkids; i++)
    gate_free(g->kids[i]);
  free(g->kids);
  free(g->var);
  free(g);
}

struct Gate *
gate_parse(struct SNode *expr)
{
  const char   *h;
  struct Gate **kids;
  int           i, n;

  if (!expr)
    return gate_new(G_TRUE);

  if (expr->kind == S_ATOM) {
    struct Gate *g = gate_new(G_VAR);
    g->var         = estrdup(expr->atom);
    return g;
  }

  h = s_head(expr);
  if (!h)
    eprintf("manifest: empty () in gate\n");

  if (strcmp(h, "not") == 0) {
    struct Gate *g;
    if (expr->nkids != 2)
      eprintf("manifest: (not X) takes one operand\n");
    g          = gate_new(G_NOT);
    g->kids    = emalloc(sizeof *g->kids);
    g->kids[0] = gate_parse(expr->kids[1]);
    g->nkids   = 1;
    return g;
  }
  if (strcmp(h, "and") == 0 || strcmp(h, "or") == 0) {
    struct Gate *g = gate_new(h[0] == 'a' ? G_AND : G_OR);
    if (expr->nkids < 2)
      eprintf("manifest: (%s ...) needs one operand\n", h);
    n    = expr->nkids - 1;
    kids = emalloc((size_t)n * sizeof *kids);
    for (i = 0; i < n; i++)
      kids[i] = gate_parse(expr->kids[i + 1]);
    g->kids  = kids;
    g->nkids = n;
    return g;
  }
  eprintf("manifest: unknown gate operator '%s'\n", h);
  return NULL;
}

/* true if the gate contains any $(...) that needs per-instance
 * expansion before it can be evaluated */
int
gate_has_dyn(struct Gate *g)
{
  int i;
  if (!g)
    return 0;
  if (g->kind == G_VAR)
    return strstr(g->var, "$(") != NULL;
  for (i = 0; i < g->nkids; i++)
    if (gate_has_dyn(g->kids[i]))
      return 1;
  return 0;
}

int
gate_eval(struct Gate *g)
{
  int i, v;
  if (!g)
    return 1;
  switch (g->kind) {
    case G_TRUE:
      return 1;
    case G_VAR: {
      char *vraw = kv_get(g->var);
      return vraw && *vraw && strcmp(vraw, "0") != 0;
    }
    case G_NOT:
      return !gate_eval(g->kids[0]);
    case G_AND:
      v = 1;
      for (i = 0; i < g->nkids; i++)
        v = v && gate_eval(g->kids[i]);
      return v;
    case G_OR:
      v = 0;
      for (i = 0; i < g->nkids; i++)
        v = v || gate_eval(g->kids[i]);
      return v;
  }
  return 0;
}

/* expand every $(...) so a gate like build_$(basestem) resolves
 * against the active overlay before evaluation */
struct Gate *
gate_materialize(struct Gate *g)
{
  int          i;
  struct Gate *out;

  if (!g)
    return NULL;
  out = gate_new(g->kind);
  if (g->var)
    out->var = kv_expand(g->var);
  if (g->nkids) {
    out->kids = emalloc((size_t)g->nkids * sizeof *out->kids);
    for (i = 0; i < g->nkids; i++)
      out->kids[i] = gate_materialize(g->kids[i]);
    out->nkids = g->nkids;
  }
  return out;
}
