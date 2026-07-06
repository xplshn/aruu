#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pull in every rule a group names, recursively */
static struct StrList visited_groups;

void
include_ref(const char *name)
{
  struct Group *g = group_find(name);
  int           i;

  if (g) {
    if (sl_has(&visited_groups, name))
      return;
    sl_push(&visited_groups, name);
    for (i = 0; i < g->refs.n; i++)
      include_ref(g->refs.v[i]);
    return;
  }
  if (rule_find(name)) {
    expand_rule(name);
    return;
  }
  eprintf("manifest: '%s' is neither a known group nor a known rule\n", name);
}

void
build_default_group(void)
{
  struct Group *def = group_find("default");
  int           i;
  if (!def)
    eprintf("no group given, and manifest defines no (group default)\n");
  for (i = 0; i < def->refs.n; i++)
    include_ref(def->refs.v[i]);
}

/* match on ALIAS when the rule gave one, otherwise on the rule own
 * bare name, so a member with no alias is still reachable under its
 * existing name */
static struct Rule *
rule_find_member(const char *group, const char *alias)
{
  int i;
  for (i = 0; i < nrules; i++) {
    struct Rule *r = &rules[i];
    if (!r->member_of[0] || strcmp(r->member_of, group) != 0)
      continue;
    if (r->member_alias[0]) {
      if (strcmp(r->member_alias, alias) == 0)
        return r;
    } else if (strcmp(r->name, alias) == 0) {
      return r;
    }
  }
  return NULL;
}

/* GROUP/ALIAS qualified addressing. leading segments before the
 * last two are ignored, so "posix/bc" and "cmd/posix/bc" resolve
 * the same rule. returns NULL when w has no '/', so callers can
 * fall through to plain group/rule resolution. a qualified path
 * that does not resolve is a hard error, not a silent fallthrough */
static struct Rule *
resolve_member_path(const char *w)
{
  const char  *last_slash;
  const char  *alias;
  const char  *group_start;
  const char  *p;
  char         group[128];
  size_t       glen;
  struct Rule *r;

  last_slash = strrchr(w, '/');
  if (!last_slash)
    return NULL;
  alias = last_slash + 1;
  if (!*alias)
    eprintf("manifest: '%s' ends in '/', expected GROUP/ALIAS\n", w);

  group_start = w;
  for (p = w; p < last_slash; p++)
    if (*p == '/')
      group_start = p + 1;
  glen = (size_t)(last_slash - group_start);
  if (glen == 0 || glen >= sizeof group)
    eprintf("manifest: '%s': bad GROUP/ALIAS path\n", w);
  memcpy(group, group_start, glen);
  group[glen] = '\0';

  if (!group_find(group))
    eprintf("manifest: '%s': no such group '%s'\n", w, group);
  r = rule_find_member(group, alias);
  if (!r)
    eprintf("manifest: '%s' names no member of group '%s'\n", alias, group);
  return r;
}

/* META.TARGET applies META overrides then resolves TARGET.
 * META alone applies META then falls back to (group default).
 * anything else is a plain group or rule name */
void
resolve_wanted(const char *w)
{
  const char  *dot = strchr(w, '.');
  struct Meta *m;
  char         meta_name[128];
  struct Rule *member;

  if (strchr(w, '/')) {
    member = resolve_member_path(w);
    expand_rule(member->name);
    return;
  }

  if (dot) {
    size_t len = (size_t)(dot - w);
    if (len >= sizeof meta_name)
      eprintf("manifest: meta name too long in '%s'\n", w);
    memcpy(meta_name, w, len);
    meta_name[len] = '\0';
    m              = meta_find(meta_name);
    if (!m)
      eprintf("manifest: '%s' names no known meta-target\n", meta_name);
    meta_apply(m);
    include_ref(dot + 1);
    return;
  }

  m = meta_find(w);
  if (m) {
    meta_apply(m);
    build_default_group();
    return;
  }

  /* a rule that declared (member ...) does not resolve by bare name
   * from the command line, only through GROUP/ALIAS or transitively
   * via its owning group ref list */
  {
    struct Rule *br = rule_find(w);
    if (br && br->member_of[0])
      eprintf(
          "manifest: '%s' is a member of group '%s', not a top-level target; "
          "use '%s/%s'\n",
          w,
          br->member_of,
          br->member_of,
          br->member_alias[0] ? br->member_alias : br->name
      );
  }

  include_ref(w);
}

/* a plain token in (dep) or (after) that names an existing rule
 * means the same as (rule NAME). resolved once after the whole
 * manifest loads, so a rule declared later can still be named
 * earlier. a token with $, glob metachars, /, or . is left alone
 * so this never shadows a real file dependency */
static int
looks_like_path(const char *s)
{
  return strpbrk(s, "$*?/.") != NULL;
}

static void
resolve_bareword_deps(struct StrList *sl)
{
  int i;
  for (i = 0; i < sl->n; i++) {
    char *tok = sl->v[i];
    char  buf[256];
    if (tok[0] == '@' || looks_like_path(tok) || !rule_find(tok))
      continue;
    snprintf(buf, sizeof buf, "@%s", tok);
    free(sl->v[i]);
    sl->v[i] = estrdup(buf);
  }
}

void
resolve_all_bareword_deps(void)
{
  int i;
  for (i = 0; i < nrules; i++) {
    resolve_bareword_deps(&rules[i].in);
    resolve_bareword_deps(&rules[i].extra_deps);
  }
}
