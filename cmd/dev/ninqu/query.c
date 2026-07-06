#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* walk every gate tree, collect the static var names and the rules
 * each one gates. skips $(...) vars since those are per-instance */
static void
collect_gate_vars(struct StrList *out, struct StrList *affects, struct StrList *rules_affected)
{
  int i;
  for (i = 0; i < nrules; i++) {
    struct Rule   *r    = &rules[i];
    struct Gate   *g    = r->gate;
    struct StrList seen = {0};
    struct Gate  **stack;
    int            sp = 0;

    if (!g)
      continue;
    stack       = emalloc(64 * sizeof *stack);
    stack[sp++] = g;
    while (sp > 0) {
      struct Gate *cur = stack[--sp];
      int          k;
      if (cur->kind == G_VAR) {
        if (strstr(cur->var, "$("))
          continue;
        if (!sl_has(&seen, cur->var)) {
          sl_push(&seen, cur->var);
          if (!sl_has(out, cur->var))
            sl_push(out, cur->var);
          sl_push(affects, cur->var);
          sl_push(rules_affected, r->name);
        }
        continue;
      }
      for (k = 0; k < cur->nkids; k++)
        stack[sp++] = cur->kids[k];
    }
    free(stack);
  }
}

/* (feature NAME) tags, separate from gate vars. a rule can carry
 * more than one, so the pairing is flattened */
static void
collect_named_features(struct StrList *out, struct StrList *owns, struct StrList *rules_tagged)
{
  int i, j;
  for (i = 0; i < nrules; i++) {
    struct Rule *r = &rules[i];
    for (j = 0; j < r->features.n; j++) {
      const char *feat = r->features.v[j];
      if (!sl_has(out, feat))
        sl_push(out, feat);
      sl_push(owns, feat);
      sl_push(rules_tagged, r->name);
    }
  }
}

void
do_features(void)
{
  struct StrList gatevars = {0}, affects = {0}, affected = {0};
  struct StrList featnames = {0}, feat_owns = {0}, feat_rules = {0};
  int            i, j;

  printf("variables (override with -Dname=value):\n");
  for (i = 0; i < kv.n; i++)
    printf(
        "  %s=%s%s\n",
        kv.v[i].key,
        kv.v[i].val,
        sl_has(&kv_locked, kv.v[i].key) ? "  [locked by -D]" : ""
    );

  collect_gate_vars(&gatevars, &affects, &affected);
  printf("\nfeature gates (override with -Dname=0 or -Dname=1):\n");
  for (i = 0; i < gatevars.n; i++) {
    printf("  %s=%s\n", gatevars.v[i], kv_get_or(gatevars.v[i], ""));
    for (j = 0; j < affects.n; j++)
      if (strcmp(affects.v[j], gatevars.v[i]) == 0)
        printf("      gates rule: %s\n", affected.v[j]);
  }

  collect_named_features(&featnames, &feat_owns, &feat_rules);
  if (featnames.n > 0) {
    printf("\nnamed features (declared with (feature NAME)):\n");
    for (i = 0; i < featnames.n; i++) {
      printf("  %s\n", featnames.v[i]);
      for (j = 0; j < feat_owns.n; j++)
        if (strcmp(feat_owns.v[j], featnames.v[i]) == 0)
          printf("      rule: %s\n", feat_rules.v[j]);
    }
  }

  printf("\ngroups:\n");
  for (i = 0; i < ngroups; i++)
    printf("  %s\n", groups[i].name);

  printf("\nrules:\n");
  for (i = 0; i < nrules; i++) {
    struct Rule *r = &rules[i];
    printf("  %s%s\n", r->name, r->globs.n > 0 ? "  (pattern rule)" : "");
    if (r->description[0])
      printf("      # %s\n", r->description);
    if (r->group_name[0])
      printf("      group: %s\n", r->group_name);
    if (r->member_of[0])
      printf("      member: %s/%s\n", r->member_of, r->member_alias[0] ? r->member_alias : r->name);
    for (j = 0; j < r->features.n; j++)
      printf("      feature: %s\n", r->features.v[j]);
  }
}

void
do_query(const char *sub, const char *arg)
{
  int i, j;

  if (strcmp(sub, "features") == 0) {
    do_features();
    return;
  }
  if (strcmp(sub, "groups") == 0) {
    for (i = 0; i < ngroups; i++) {
      printf("%s\n", groups[i].name);
      for (j = 0; j < groups[i].refs.n; j++)
        printf("  %s\n", groups[i].refs.v[j]);
    }
    return;
  }
  if (strcmp(sub, "metas") == 0) {
    for (i = 0; i < nmetas; i++) {
      printf("%s\n", metas[i].name);
      for (j = 0; j < metas[i].keys.n; j++)
        printf("  %s=%s\n", metas[i].keys.v[j], metas[i].vals.v[j]);
    }
    return;
  }
  if (strcmp(sub, "rules") == 0) {
    for (i = 0; i < nrules; i++) {
      printf("%s", rules[i].name);
      if (rules[i].globs.n > 0) {
        printf("  glob:");
        for (j = 0; j < rules[i].globs.n; j++)
          printf(" %s", rules[i].globs.v[j]);
      }
      if (rules[i].produces[0])
        printf("  produces=%s", rules[i].produces);
      if (rules[i].group_name[0])
        printf("  group=%s", rules[i].group_name);
      if (rules[i].member_of[0])
        printf(
            "  member=%s/%s",
            rules[i].member_of,
            rules[i].member_alias[0] ? rules[i].member_alias : rules[i].name
        );
      for (j = 0; j < rules[i].features.n; j++)
        printf("  feature=%s", rules[i].features.v[j]);
      if (rules[i].description[0])
        printf("  # %s", rules[i].description);
      printf("\n");
    }
    return;
  }
  if (strcmp(sub, "graph") == 0) {
    printf("digraph ninqu {\n");
    if (arg) {
      struct Group *g = group_find(arg);
      if (!g)
        eprintf("query graph: no such group '%s'\n", arg);
      for (i = 0; i < g->refs.n; i++) {
        struct Rule *r = rule_find(g->refs.v[i]);
        if (!r)
          continue;
        for (j = 0; j < r->in.n; j++)
          if (r->in.v[j][0] == '@')
            printf("  \"%s\" -> \"%s\";\n", r->in.v[j] + 1, r->name);
        for (j = 0; j < r->extra_deps.n; j++)
          if (r->extra_deps.v[j][0] == '@')
            printf("  \"%s\" -> \"%s\" [style=dashed];\n", r->extra_deps.v[j] + 1, r->name);
      }
    } else {
      for (i = 0; i < nrules; i++) {
        for (j = 0; j < rules[i].in.n; j++)
          if (rules[i].in.v[j][0] == '@')
            printf("  \"%s\" -> \"%s\";\n", rules[i].in.v[j] + 1, rules[i].name);
        for (j = 0; j < rules[i].extra_deps.n; j++)
          if (rules[i].extra_deps.v[j][0] == '@')
            printf(
                "  \"%s\" -> \"%s\" [style=dashed];\n", rules[i].extra_deps.v[j] + 1, rules[i].name
            );
      }
    }
    printf("}\n");
    return;
  }
  eprintf("query: unknown subcommand '%s' (try features, groups, metas, rules, graph)\n", sub);
}
