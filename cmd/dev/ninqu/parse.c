#include "ninqu.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct StrList files_read;

/* second child must be an atom, more children may follow */
const char *
first_atom(struct SNode *node, const char *msg)
{
  if (node->nkids < 2 || node->kids[1]->kind != S_ATOM)
    eprintf("manifest: %s\n", msg);
  return node->kids[1]->atom;
}

/* second child must be the only other child, and an atom */
const char *
sole_atom(struct SNode *node, const char *msg)
{
  if (node->nkids != 2 || node->kids[1]->kind != S_ATOM)
    eprintf("manifest: %s\n", msg);
  return node->kids[1]->atom;
}

/* a dep operand is a bare atom, a (rule NAME) stored as @NAME, or
 * an (after x...) whose operands are order-only */
static void
parse_dep_item(struct SNode *item, struct StrList *hard, struct StrList *ord, int allow_after)
{
  const char *h;

  if (item->kind == S_ATOM) {
    sl_push(hard, item->atom);
    return;
  }
  h = s_head(item);
  if (!h)
    eprintf("manifest: empty () in dep\n");
  if (strcmp(h, "rule") == 0) {
    char buf[256];
    snprintf(buf, sizeof buf, "@%s", sole_atom(item, "(rule NAME) takes one name"));
    sl_push(hard, buf);
    return;
  }
  if (strcmp(h, "after") == 0) {
    int i;
    if (!allow_after)
      eprintf("manifest: (after ...) not allowed here\n");
    for (i = 1; i < item->nkids; i++)
      parse_dep_item(item->kids[i], ord, ord, 0);
    return;
  }
  eprintf("manifest: unknown dep operand '%s'\n", h);
}

static void
parse_dep_clause(struct SNode *clause, struct StrList *hard, struct StrList *ord)
{
  int i;
  for (i = 1; i < clause->nkids; i++)
    parse_dep_item(clause->kids[i], hard, ord, 1);
}

void
parse_exec_clause(struct SNode *clause, struct Cmd *cmd)
{
  int i;
  memset(cmd, 0, sizeof *cmd);
  for (i = 1; i < clause->nkids; i++) {
    if (clause->kids[i]->kind != S_ATOM)
      eprintf("manifest: (exec ...) operands must be atoms\n");
    sl_push(&cmd->argv, clause->kids[i]->atom);
  }
  if (cmd->argv.n == 0)
    eprintf("manifest: (exec ...) needs one operand\n");
}

/* desugar to (exec sh -c STR). sh is the posix shell, overridable
 * by writing (exec ...) directly */
void
parse_shell_clause(struct SNode *clause, struct Cmd *cmd)
{
  memset(cmd, 0, sizeof *cmd);
  sl_push(&cmd->argv, "sh");
  sl_push(&cmd->argv, "-c");
  sl_push(&cmd->argv, sole_atom(clause, "(shell \"...\") takes one string"));
}

static void
parse_pipe_stage(struct SNode *stage, struct StrList *argv)
{
  const char *h = s_head(stage);
  int         j;

  if (!h)
    eprintf("manifest: (pipe ...) stage must be a form\n");
  if (strcmp(h, "exec") == 0) {
    for (j = 1; j < stage->nkids; j++) {
      if (stage->kids[j]->kind != S_ATOM)
        eprintf("manifest: (exec ...) operands must be atoms\n");
      sl_push(argv, stage->kids[j]->atom);
    }
  } else if (strcmp(h, "shell") == 0) {
    sl_push(argv, "sh");
    sl_push(argv, "-c");
    sl_push(argv, sole_atom(stage, "(shell \"...\") takes one string"));
  } else {
    eprintf("manifest: (pipe) stage must be exec or shell, got '%s'\n", h);
  }
  if (argv->n == 0)
    eprintf("manifest: (pipe) stage cannot be empty\n");
}

void
parse_pipe_clause(struct SNode *clause, struct Cmd *cmd)
{
  int i, j;
  memset(cmd, 0, sizeof *cmd);
  cmd->is_pipe = 1;
  for (i = 1; i < clause->nkids; i++) {
    struct StrList  argv = {0};
    struct StrList *slot;

    parse_pipe_stage(clause->kids[i], &argv);

    slot = emalloc(sizeof *slot);
    memset(slot, 0, sizeof *slot);
    for (j = 0; j < argv.n; j++)
      sl_push(slot, argv.v[j]);
    if (cmd->nstages == 0)
      cmd->stages = emalloc(8 * sizeof *cmd->stages);
    else if (cmd->nstages >= 8)
      cmd->stages = erealloc(cmd->stages, (size_t)cmd->nstages * 2 * sizeof *cmd->stages);
    cmd->stages[cmd->nstages++] = slot;
  }
  if (cmd->nstages < 2)
    eprintf("manifest: (pipe ...) needs two stages\n");
  for (i = 0; i < cmd->nstages; i++) {
    char *joined = sl_join(cmd->stages[i]);
    if (i > 0)
      sl_push(&cmd->argv, "|");
    sl_push(&cmd->argv, joined);
    free(joined);
  }
}

static void apply_rule_clause(struct Rule *r, const char *name, struct SNode *clause);

static void
process_rule_body(struct Rule *r, const char *name, struct SNode *form, int start)
{
  int i;
  for (i = start; i < form->nkids; i++)
    apply_rule_clause(r, name, form->kids[i]);
}

/* (if EXPR clause...) splices its clauses in when EXPR is true
 * reuses the gate grammar so there is one condition language
 * a $(...) in the condition is rejected: splicing per glob match
 * would mean deferring cmd/dep construction, which no rule needs
 * use (gate ...) on a separate rule for a per-instance condition */
static void
apply_if_clause(struct Rule *r, const char *name, struct SNode *clause)
{
  struct Gate *cond;

  if (clause->nkids < 2)
    eprintf("manifest: rule %s: (if EXPR clause...) needs a condition\n", name);
  cond = gate_parse(clause->kids[1]);
  if (gate_has_dyn(cond))
    eprintf(
        "manifest: rule %s: (if ...) condition cannot reference $(...), "
        "only (gate ...) on the whole rule supports a per-instance condition\n",
        name
    );
  if (gate_eval(cond))
    process_rule_body(r, name, clause, 2);
  gate_free(cond);
}

static void
apply_rule_clause(struct Rule *r, const char *name, struct SNode *clause)
{
  const char *h = s_head(clause);
  if (!h)
    eprintf("manifest: rule %s: empty () clause\n", name);

  if (strcmp(h, "if") == 0) {
    apply_if_clause(r, name, clause);
  } else if (strcmp(h, "glob") == 0) {
    int j;
    for (j = 1; j < clause->nkids; j++) {
      if (clause->kids[j]->kind != S_ATOM)
        eprintf("manifest: (glob ...) operands must be atoms\n");
      sl_push(&r->globs, clause->kids[j]->atom);
    }
  } else if (strcmp(h, "skip") == 0) {
    int j;
    for (j = 1; j < clause->nkids; j++) {
      if (clause->kids[j]->kind != S_ATOM)
        eprintf("manifest: (skip ...) operands must be atoms\n");
      sl_push(&r->skip, clause->kids[j]->atom);
    }
  } else if (strcmp(h, "mark") == 0) {
    strlcpy(
        r->require_marker,
        sole_atom(clause, "(mark STR) takes one string"),
        sizeof r->require_marker
    );
  } else if (strcmp(h, "req") == 0) {
    strlcpy(
        r->require_file, sole_atom(clause, "(req PATH) takes one path"), sizeof r->require_file
    );
  } else if (strcmp(h, "gate") == 0) {
    if (clause->nkids != 2)
      eprintf("manifest: (gate EXPR) takes one expression\n");
    r->gate = gate_parse(clause->kids[1]);
  } else if (strcmp(h, "dep") == 0) {
    parse_dep_clause(clause, &r->in, &r->extra_deps);
  } else if (strcmp(h, "after") == 0) {
    parse_dep_clause(clause, &r->extra_deps, &r->extra_deps);
  } else if (strcmp(h, "out") == 0) {
    strlcpy(r->out, sole_atom(clause, "(out TEMPLATE) takes one template"), sizeof r->out);
  } else if (strcmp(h, "exec") == 0) {
    parse_exec_clause(clause, &r->cmd);
  } else if (strcmp(h, "shell") == 0) {
    parse_shell_clause(clause, &r->cmd);
  } else if (strcmp(h, "pipe") == 0) {
    parse_pipe_clause(clause, &r->cmd);
  } else if (strcmp(h, "dir") == 0) {
    strlcpy(r->workdir, sole_atom(clause, "(dir PATH) takes one path"), sizeof r->workdir);
  } else if (strcmp(h, "phony") == 0) {
    r->phony = 1;
  } else if (strcmp(h, "redirect") == 0) {
    r->redirect = 1;
  } else if (strcmp(h, "produces") == 0) {
    strlcpy(r->produces, sole_atom(clause, "(produces NAME) takes one name"), sizeof r->produces);
  } else if (strcmp(h, "description") == 0) {
    strlcpy(
        r->description,
        sole_atom(clause, "(description STR) takes one string"),
        sizeof r->description
    );
  } else if (strcmp(h, "group") == 0) {
    /* display-only: shows up in (query rules) output, no effect on
 * resolution. (member ...) below is the enforcing one. the two
 * are separate because several rules used (group ...) this way
 * before namespacing existed */
    strlcpy(r->group_name, sole_atom(clause, "(group NAME) takes one name"), sizeof r->group_name);
  } else if (strcmp(h, "member") == 0) {
    /* (member GROUP [ALIAS]) hides the bare name from the command
 * line. ALIAS defaults to the rule own name */
    if (clause->nkids < 2 || clause->nkids > 3 || clause->kids[1]->kind != S_ATOM)
      eprintf("manifest: rule %s: (member GROUP [ALIAS]) needs a group name\n", name);
    strlcpy(r->member_of, clause->kids[1]->atom, sizeof r->member_of);
    if (clause->nkids == 3) {
      if (clause->kids[2]->kind != S_ATOM)
        eprintf("manifest: rule %s: (member GROUP ALIAS) alias must be an atom\n", name);
      strlcpy(r->member_alias, clause->kids[2]->atom, sizeof r->member_alias);
    }
  } else if (strcmp(h, "feature") == 0) {
    sl_push(&r->features, sole_atom(clause, "(feature NAME) takes one name"));
  } else {
    eprintf("manifest: rule %s: unknown clause '%s'\n", name, h);
  }
}

static void
parse_rule(struct SNode *form)
{
  struct Rule *r;
  const char  *name;

  name = first_atom(form, "(rule NAME ...) needs a name");
  r    = rule_new(name);
  process_rule_body(r, name, form, 2);
}

static void
parse_group(struct SNode *form)
{
  struct Group *g;
  int           i;
  const char   *name;

  name = first_atom(form, "(group NAME ...) needs a name");
  g    = group_new(name);

  for (i = 2; i < form->nkids; i++) {
    struct SNode *ref = form->kids[i];
    const char   *h;

    if (ref->kind == S_ATOM) {
      sl_push(&g->refs, ref->atom);
      continue;
    }
    h = s_head(ref);
    if (!h)
      eprintf("manifest: group %s: empty () ref\n", name);
    if ((strcmp(h, "rule") == 0 || strcmp(h, "group") == 0) && ref->nkids == 2
        && ref->kids[1]->kind == S_ATOM) {
      sl_push(&g->refs, ref->kids[1]->atom);
      continue;
    }
    eprintf("manifest: group %s: unknown ref '%s'\n", name, h);
  }
}

/* stores K/V verbatim, unexpanded. expansion happens in meta_apply
 * at target-resolution time, so a value picks up whatever its vars
 * hold then, not what they held at load time */
static void
parse_meta(struct SNode *form)
{
  struct Meta *m;
  int          i;
  const char  *name;

  name = first_atom(form, "(meta NAME ...) needs a name");
  m    = meta_new(name);

  for (i = 2; i < form->nkids; i++) {
    struct SNode *clause = form->kids[i];
    const char   *h      = s_head(clause);
    if (!h || strcmp(h, "set") != 0)
      eprintf("manifest: meta %s: only (set NAME VAL) clauses are allowed\n", name);
    if (clause->nkids != 3 || clause->kids[1]->kind != S_ATOM || clause->kids[2]->kind != S_ATOM)
      eprintf("manifest: meta %s: (set NAME VAL) needs a name and an atom value\n", name);
    sl_push(&m->keys, clause->kids[1]->atom);
    sl_push(&m->vals, clause->kids[2]->atom);
  }
}

void
meta_apply(struct Meta *m)
{
  int i;
  for (i = 0; i < m->keys.n; i++) {
    char *exp = kv_expand(m->vals.v[i]);
    kv_set_manifest(m->keys.v[i], exp);
    free(exp);
  }
}

static void
parse_set(struct SNode *form)
{
  char buf[8192];

  first_atom(form, "(set NAME VAL) needs a name");
  if (form->nkids != 3)
    eprintf("manifest: (set NAME VAL) takes one value\n");

  if (form->kids[2]->kind == S_LIST) {
    const char *h = s_head(form->kids[2]);
    if (h && strcmp(h, "capture") == 0) {
      struct SNode *cap = form->kids[2];
      struct Cmd    cmd;
      char         *out;

      if (cap->nkids != 2)
        eprintf("manifest: (capture (exec ...)) takes one form\n");
      h = s_head(cap->kids[1]);
      if (!h)
        eprintf("manifest: capture operand must be a form\n");
      if (strcmp(h, "exec") == 0)
        parse_exec_clause(cap->kids[1], &cmd);
      else if (strcmp(h, "shell") == 0)
        parse_shell_clause(cap->kids[1], &cmd);
      else
        eprintf("manifest: capture operand must be exec or shell\n");
      out = capture_argv(&cmd.argv);
      strlcpy(buf, out, sizeof buf);
      free(out);
      kv_set_manifest(form->kids[1]->atom, buf);
      return;
    }
    eprintf("manifest: (set NAME VAL): value must be atom or (capture ...)\n");
  }
  kv_set_manifest(form->kids[1]->atom, form->kids[2]->atom);
}

/* (list) and (append) keep a space-joined string in the kv store
 * a $(NAME) alone in a command splits back into argv entries */
static void
parse_list_or_append(struct SNode *form, int append)
{
  const char *name;
  char        buf[8192];
  char        msg[64];
  size_t      len = 0;
  int         i;
  char       *prev;

  snprintf(msg, sizeof msg, "(%s NAME tok...) needs a name", form->kids[0]->atom);
  name   = first_atom(form, msg);
  buf[0] = '\0';

  if (append) {
    prev = kv_get(name);
    if (prev)
      len = strlcpy(buf, prev, sizeof buf);
  }
  for (i = 2; i < form->nkids; i++) {
    struct SNode *tok = form->kids[i];
    char         *exp;
    if (tok->kind != S_ATOM)
      eprintf("manifest: (%s NAME tok...): tokens must be atoms\n", form->kids[0]->atom);
    if (len > 0 && len + 1 < sizeof buf)
      buf[len++] = ' ';
    exp = kv_expand(tok->atom);
    len += strlcpy(buf + len, exp, sizeof buf - len);
    free(exp);
  }
  kv_set_manifest(name, buf);
}

static void
parse_map(struct SNode *form)
{
  struct Map *m;
  int         i;
  const char *name;

  name = first_atom(form, "(map NAME (k v)...) needs a name");
  m    = map_find(name);
  if (!m)
    m = map_new(name);

  for (i = 2; i < form->nkids; i++) {
    struct SNode *pair = form->kids[i];
    if (!s_is_list(pair) || pair->nkids != 2 || pair->kids[0]->kind != S_ATOM
        || pair->kids[1]->kind != S_ATOM)
      eprintf("manifest: (map %s ...) entries must be (key value)\n", name);
    kv_set_raw(&m->entries, pair->kids[0]->atom, pair->kids[1]->atom);
  }
}

static void
parse_import(struct SNode *form)
{
  char        rp[PATH_MAX];
  const char *path;

  path = sole_atom(form, "(import PATH) takes one path");

  if (realpath(path, rp) && sl_has(&files_read, rp))
    return;

  materialize_if_missing(path);
  if (!file_exists(path))
    eprintf("manifest: (import %s): missing and no rule produces it\n", path);
  load_file(path);
}

static void
parse_set_file(struct SNode *form)
{
  const char *path;
  FILE       *fp;
  char        line[8192];
  char        rp[PATH_MAX];

  path = sole_atom(form, "(set-file PATH) takes one path");

  materialize_if_missing(path);
  if (!file_exists(path))
    eprintf("manifest: (set-file %s): missing and no rule produces it\n", path);

  fp = fopen(path, "r");
  if (!fp)
    eprintf("manifest: (set-file %s):", path);
  if (realpath(path, rp))
    sl_push(&files_read, rp);

  while (fgets(line, sizeof line, fp)) {
    char *s = line, *eq, *end;
    while (isspace((unsigned char)*s))
      s++;
    if (*s == '\0' || *s == '#')
      continue;
    eq = strchr(s, '=');
    if (!eq)
      continue;
    *eq = '\0';
    end = eq - 1;
    while (end > s && isspace((unsigned char)*end))
      *end-- = '\0';
    eq++;
    while (isspace((unsigned char)*eq))
      eq++;
    end = eq + strlen(eq);
    while (end > eq && (isspace((unsigned char)end[-1]) || end[-1] == '\n'))
      *--end = '\0';
    kv_set_manifest(s, eq);
  }
  fclose(fp);
}

/* (template NAME (PARAM...) FORM...): the body is cloned so it stays
 * valid regardless of when or how many times (use ...) instantiates
 * it */
static void
parse_template(struct SNode *form)
{
  struct Template *t;
  struct SNode    *params;
  const char      *name;
  int              i;

  name = first_atom(form, "(template NAME (PARAM...) FORM...) needs a name");
  if (form->nkids < 3 || form->kids[2]->kind != S_LIST)
    eprintf("manifest: template %s: needs a (PARAM...) list\n", name);
  params = form->kids[2];

  t = template_new(name);
  for (i = 0; i < params->nkids; i++) {
    if (params->kids[i]->kind != S_ATOM)
      eprintf("manifest: template %s: param names must be atoms\n", name);
    sl_push(&t->params, params->kids[i]->atom);
  }
  t->body = snode_list(form->line);
  for (i = 3; i < form->nkids; i++)
    snode_push(t->body, snode_clone(form->kids[i]));
}

/* clone the template body, substitute each $(PARAM), then dispatch
 * every substituted form as a normal top-level form */
static void
parse_use(struct SNode *form)
{
  struct Template *t;
  struct SNode    *clone;
  const char      *name;
  char           **vals;
  int              i, nargs;

  name = first_atom(form, "(use NAME arg...) needs a name");
  t    = template_find(name);
  if (!t)
    eprintf("manifest: (use %s ...): no such template\n", name);

  nargs = form->nkids - 2;
  if (nargs != t->params.n)
    eprintf("manifest: (use %s ...): takes %d argument(s), got %d\n", name, t->params.n, nargs);

  vals = emalloc((size_t)(nargs ? nargs : 1) * sizeof *vals);
  for (i = 0; i < nargs; i++) {
    if (form->kids[i + 2]->kind != S_ATOM)
      eprintf("manifest: (use %s ...): arguments must be atoms\n", name);
    vals[i] = form->kids[i + 2]->atom;
  }

  clone = snode_clone(t->body);
  snode_subst(clone, &t->params, vals);
  for (i = 0; i < clone->nkids; i++)
    dispatch_top_form(clone->kids[i]);
  snode_free(clone);
  free(vals);
}

void
dispatch_top_form(struct SNode *form)
{
  const char *h = s_head(form);
  if (!h)
    eprintf("manifest: top-level form has no head\n");
  if (strcmp(h, "set") == 0)
    parse_set(form);
  else if (strcmp(h, "list") == 0)
    parse_list_or_append(form, 0);
  else if (strcmp(h, "append") == 0)
    parse_list_or_append(form, 1);
  else if (strcmp(h, "map") == 0)
    parse_map(form);
  else if (strcmp(h, "rule") == 0)
    parse_rule(form);
  else if (strcmp(h, "group") == 0)
    parse_group(form);
  else if (strcmp(h, "meta") == 0)
    parse_meta(form);
  else if (strcmp(h, "import") == 0)
    parse_import(form);
  else if (strcmp(h, "set-file") == 0)
    parse_set_file(form);
  else if (strcmp(h, "template") == 0)
    parse_template(form);
  else if (strcmp(h, "use") == 0)
    parse_use(form);
  else
    eprintf("manifest: unknown top-level form '%s'\n", h);
}

void
load_manifest(struct SNode *root)
{
  int i;
  for (i = 0; i < root->nkids; i++)
    dispatch_top_form(root->kids[i]);
}

void
load_file(const char *path)
{
  FILE         *fp;
  char         *buf;
  size_t        cap, len;
  char          rp[PATH_MAX];
  struct SLex   lx;
  struct SNode *root;

  if (realpath(path, rp)) {
    if (sl_has(&files_read, rp))
      return;
    sl_push(&files_read, rp);
  }

  fp = fopen(path, "r");
  if (!fp)
    eprintf("cannot open manifest %s:", path);

  cap = 65536;
  len = 0;
  buf = emalloc(cap);
  for (;;) {
    size_t n;
    if (len + 65536 > cap) {
      cap *= 2;
      buf = erealloc(buf, cap);
    }
    n = fread(buf + len, 1, cap - len - 1, fp);
    len += n;
    if (n == 0)
      break;
  }
  buf[len] = '\0';
  fclose(fp);

  lx.src  = buf;
  lx.pos  = 0;
  lx.line = 1;
  root    = snode_parse(&lx);
  free(buf);
  load_manifest(root);
  snode_free(root);
}
