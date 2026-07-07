#include "ninqu.h"

#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
dep_push(struct Inst *inst, int idx)
{
  int i;
  for (i = 0; i < inst->n_dep; i++)
    if (inst->dep_inst[i] == idx)
      return;
  if (inst->n_dep >= inst->cap_dep) {
    inst->cap_dep  = inst->cap_dep ? inst->cap_dep * 2 : 4;
    inst->dep_inst = erealloc(inst->dep_inst, (size_t)inst->cap_dep * sizeof *inst->dep_inst);
  }
  inst->dep_inst[inst->n_dep++] = idx;
}

void
resolve_deps(void)
{
  int i, j, k;
  for (i = 0; i < ninsts; i++) {
    struct Inst *inst = &insts[i];
    for (j = 0; j < inst->dep_rule_names.n; j++) {
      struct Rule *dep = rule_find(inst->dep_rule_names.v[j]);
      if (!dep)
        continue;
      for (k = 0; k < dep->n_inst; k++)
        dep_push(inst, dep->inst_idx[k]);
    }
  }
}

int
inst_stale(struct Inst *inst)
{
  int i;
  if (inst->phony)
    return 1;
  if (!file_exists(inst->out))
    return 1;
  for (i = 0; i < inst->in.n; i++)
    if (mtime_of(inst->in.v[i]) > mtime_of(inst->out))
      return 1;
  for (i = 0; i < inst->stale_extra.n; i++)
    if (file_exists(inst->stale_extra.v[i])
        && mtime_of(inst->stale_extra.v[i]) > mtime_of(inst->out))
      return 1;
  return 0;
}

/* pure $(NAME) splits on whitespace so $(CFLAGS) becomes many flags, -I$(INCDIR) stays one token */
static int
is_pure_ref(const char *tok)
{
  size_t      len;
  int         depth;
  const char *p;

  if (tok[0] != '$' || tok[1] != '(')
    return 0;
  len = strlen(tok);
  if (tok[len - 1] != ')')
    return 0;
  depth = 1;
  for (p = tok + 2; *p; p++) {
    if (p[0] == '$' && p[1] == '(') {
      depth++;
      p++;
      continue;
    }
    if (*p == ')') {
      depth--;
      if (depth == 0)
        return (size_t)(p - tok) == len - 1;
    }
  }
  return 0;
}

static void
expand_cmd_token(struct StrList *out, const char *raw)
{
  char *exp = kv_expand(raw);
  if (is_pure_ref(raw)) {
    struct StrList pieces = {0};
    int            j;
    sl_split(&pieces, exp);
    for (j = 0; j < pieces.n; j++)
      sl_push(out, pieces.v[j]);
  } else {
    sl_push(out, exp);
  }
  free(exp);
}

static void
build_cmd(struct Inst *inst, struct Rule *r)
{
  int i;
  if (r->cmd.is_pipe) {
    inst->cmd.is_pipe = 1;
    inst->cmd.nstages = r->cmd.nstages;
    inst->cmd.stages  = emalloc((size_t)r->cmd.nstages * sizeof *inst->cmd.stages);
    for (i = 0; i < r->cmd.nstages; i++) {
      int             j;
      struct StrList *slot = emalloc(sizeof *slot);
      memset(slot, 0, sizeof *slot);
      for (j = 0; j < r->cmd.stages[i]->n; j++)
        expand_cmd_token(slot, r->cmd.stages[i]->v[j]);
      inst->cmd.stages[i] = slot;
    }
    for (i = 0; i < inst->cmd.nstages; i++) {
      char *joined = sl_join(inst->cmd.stages[i]);
      if (i > 0)
        sl_push(&inst->cmd.argv, "|");
      sl_push(&inst->cmd.argv, joined);
      free(joined);
    }
  } else {
    for (i = 0; i < r->cmd.argv.n; i++)
      expand_cmd_token(&inst->cmd.argv, r->cmd.argv.v[i]);
  }
}

/* if path does not exist, try to find a producer and run it first */
static void
resolve_input_token(int inst_idx, const char *raw, int only_dep)
{
  char *exp = kv_expand(raw);

  if (exp[0] == '@') {
    const char     *rulename = exp + 1;
    struct Rule    *dep;
    struct KvStore *saved = local_overlay;
    struct Inst    *inst;
    int             i;

    /* expand_rule may set/clear local_overlay for its own loop */
    local_overlay = NULL;
    expand_rule(rulename);
    local_overlay = saved;

    /* insts[] may have moved during expand_rule */
    inst = &insts[inst_idx];
    dep  = rule_find(rulename);
    if (!dep)
      eprintf(
          "manifest: '%s' references unknown rule '@%s'\n", rules[inst->rule_idx].name, rulename
      );
    if (!sl_has(&inst->dep_rule_names, rulename))
      sl_push(&inst->dep_rule_names, rulename);
    if (!only_dep)
      for (i = 0; i < dep->n_inst; i++)
        sl_push(&inst->in, insts[dep->inst_idx[i]].out);
  } else if (strncmp(exp, "glob:", 5) == 0) {
    struct StrList matches = {0};
    int            i;
    glob_expand(exp + 5, &matches);
    for (i = 0; i < matches.n; i++) {
      if (only_dep)
        sl_push(&insts[inst_idx].stale_extra, matches.v[i]);
      else
        sl_push(&insts[inst_idx].in, matches.v[i]);
    }
  } else {
    struct StrList pieces = {0};
    int            i;
    sl_split(&pieces, exp);
    for (i = 0; i < pieces.n; i++) {
      const char *path = pieces.v[i];
      if (!file_exists(path))
        materialize_if_missing(path);
      if (only_dep)
        sl_push(&insts[inst_idx].stale_extra, path);
      else
        sl_push(&insts[inst_idx].in, path);
    }
  }
  free(exp);
}

static void
setup_inst(struct Inst *inst, struct Rule *r)
{
  inst->phony    = r->phony;
  inst->redirect = r->redirect;
  if (r->workdir[0]) {
    char *w = kv_expand(r->workdir);
    strlcpy(inst->workdir, w, sizeof inst->workdir);
    free(w);
  }
}

/* caller must have local_overlay set if needed */
static void
finalize_inst(int idx, struct Rule *r, struct KvStore *ov)
{
  struct Inst *inst = &insts[idx];
  int          k;

  for (k = 0; k < r->in.n; k++)
    resolve_input_token(idx, r->in.v[k], 0);
  for (k = 0; k < r->extra_deps.n; k++)
    resolve_input_token(idx, r->extra_deps.v[k], 1);

  inst = &insts[idx]; /* may have moved */
  {
    char *o = kv_expand(r->out);
    strlcpy(inst->out, o, sizeof inst->out);
    free(o);
  }
  kv_set_raw(ov, "IN", sl_join(&inst->in));
  kv_set_raw(ov, "OUT", inst->out);
  build_cmd(inst, r);
}

/* true when deps (out) matches selfs glob. glob_all() needs bytes on disk
 * so dep must run synchronously before enumerating instances */
static int
dep_feeds_glob(struct Rule *self, struct Rule *dep)
{
  int j;

  if (!dep->out[0])
    return 0;
  for (j = 0; j < self->globs.n; j++) {
    char *pat = kv_expand(self->globs.v[j]);
    char *out = kv_expand(dep->out);
    int   hit = fnmatch(pat, out, 0) == 0;
    free(pat);
    free(out);
    if (hit)
      return 1;
  }
  return 0;
}

void
expand_rule(const char *name)
{
  struct Rule *r = rule_find(name);
  int          i;

  if (!r)
    eprintf("manifest: unknown rule '%s'\n", name);
  if (r->expanded)
    return;
  r->expanded = 1;

  /* static gate checked once, if false rule produces zero instances */
  if (r->gate && !gate_has_dyn(r->gate) && !gate_eval(r->gate))
    return;

  if (r->require_file[0] && r->globs.n == 0) {
    char *rf = kv_expand(r->require_file);
    int   ok = file_exists(rf);
    free(rf);
    if (!ok)
      return;
  }

  if (r->globs.n > 0) {
    struct StrList matches = {0};
    int            j;
    int            r_literal = rule_is_literal(r);

    /* run a codegen producer before globbing only when its own
     * output feeds this rules glob, so a step like POSIX_BC_C
     * (produces cmd/posix/bc.c) finishes before glob_all() looks
     * for cmd/posix/bc.c. anything else in extra_deps (a library to
     * link, a header some other rule includes) never needs to run
     * here, expand_rule() on it just registers its instances so
     * the resolve_input_token() pass in finalize_inst() can wire up
     * dep_rule_names for backend scheduling. skipped in
     * summary mode since -S must not execute */
    if (!summary_mode) {
      for (j = 0; j < r->extra_deps.n; j++) {
        char *tok = kv_expand(r->extra_deps.v[j]);
        if (tok[0] == '@') {
          struct Rule *dep = rule_find(tok + 1);
          if (dep) {
            expand_rule(dep->name);
            if (dep_feeds_glob(r, dep) && dep->n_inst > 0) {
              int k;
              for (k = 0; k < dep->n_inst; k++)
                run_inst_with_deps(dep->inst_idx[k]);
            }
          }
        }
        free(tok);
      }
    }

    glob_all(r, &matches);

    for (i = 0; i < matches.n; i++) {
      const char    *match = matches.v[i];
      const char    *base  = base_of(match);
      char           dir[1024], stem[512], basestem[512], ext[64], unused[64];
      char           ident[512];
      struct KvStore ov;
      int            idx;
      struct Inst   *inst;

      if (sl_has(&r->skip, base))
        continue;
      if (!r_literal && path_claimed_elsewhere(match, r))
        continue;
      if (r->require_marker[0] && !file_contains(match, r->require_marker))
        continue;

      if (strrchr(match, '/')) {
        size_t dl = (size_t)(strrchr(match, '/') - match);
        if (dl >= sizeof dir)
          dl = sizeof dir - 1;
        memcpy(dir, match, dl);
        dir[dl] = '\0';
      } else {
        strlcpy(dir, ".", sizeof dir);
      }
      splitext(base, basestem, sizeof basestem, ext, sizeof ext);
      splitext(match, stem, sizeof stem, unused, sizeof unused);
      to_ident(basestem, ident, sizeof ident);

      memset(&ov, 0, sizeof ov);
      kv_set_raw(&ov, "MATCH", match);
      kv_set_raw(&ov, "DIR", dir);
      kv_set_raw(&ov, "BASE", base);
      kv_set_raw(&ov, "EXT", ext);
      kv_set_raw(&ov, "STEM", stem);
      kv_set_raw(&ov, "BASESTEM", basestem);
      kv_set_raw(&ov, "IDENT", ident);
      local_overlay = &ov;

      if (r->gate && gate_has_dyn(r->gate)) {
        struct Gate *mg = gate_materialize(r->gate);
        int          ok = gate_eval(mg);
        gate_free(mg);
        if (!ok) {
          local_overlay = NULL;
          continue;
        }
      }
      if (r->require_file[0]) {
        char *rf = kv_expand(r->require_file);
        int   ok = file_exists(rf);
        free(rf);
        if (!ok) {
          local_overlay = NULL;
          continue;
        }
      }

      idx  = inst_new((int)(r - rules));
      inst = &insts[idx];
      setup_inst(inst, r);
      finalize_inst(idx, r, &ov);
      local_overlay = NULL;
    }
  } else {
    int            idx  = inst_new((int)(r - rules));
    struct Inst   *inst = &insts[idx];
    struct KvStore ov;

    /* non-glob rules have no MATCH/DIR/BASE to expose, so the
     * overlay only carries IN/OUT for build_cmd */
    setup_inst(inst, r);
    memset(&ov, 0, sizeof ov);
    local_overlay = &ov;
    finalize_inst(idx, r, &ov);
    local_overlay = NULL;
  }
}
