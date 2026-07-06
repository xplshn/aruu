#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Rule     *rules;
int              nrules, rulescap;
struct Group    *groups;
int              ngroups, groupscap;
struct Meta     *metas;
int              nmetas, metascap;
struct Template *templates;
int              ntemplates, templatescap;
struct Inst     *insts;
int              ninsts, instscap;

struct Rule *
rule_find(const char *name)
{
  int i;
  for (i = 0; i < nrules; i++)
    if (strcmp(rules[i].name, name) == 0)
      return &rules[i];
  return NULL;
}

/* match (out) against path, expanding $(...) as needed. used to
 * find a producer for a file that does not exist yet */
struct Rule *
rule_find_output(const char *path)
{
  int i;
  for (i = 0; i < nrules; i++) {
    if (!rules[i].out[0])
      continue;
    if (strchr(rules[i].out, '$')) {
      char *exp = kv_expand(rules[i].out);
      int   ok  = strcmp(exp, path) == 0;
      free(exp);
      if (ok)
        return &rules[i];
    } else if (strcmp(rules[i].out, path) == 0) {
      return &rules[i];
    }
  }
  return NULL;
}

struct Rule *
rule_find_produces(const char *name)
{
  int i;
  for (i = 0; i < nrules; i++)
    if (rules[i].produces[0] && strcmp(rules[i].produces, name) == 0)
      return &rules[i];
  return NULL;
}

struct Rule *
rule_new(const char *name)
{
  struct Rule *r;
  if (rule_find(name))
    eprintf("manifest: duplicate rule '%s'\n", name);
  if (group_find(name))
    eprintf("manifest: name '%s' already a group\n", name);
  GROW(rules, nrules, rulescap, 16);
  r = &rules[nrules++];
  memset(r, 0, sizeof *r);
  strlcpy(r->name, name, sizeof r->name);
  return r;
}

struct Group *
group_find(const char *name)
{
  int i;
  for (i = 0; i < ngroups; i++)
    if (strcmp(groups[i].name, name) == 0)
      return &groups[i];
  return NULL;
}

struct Group *
group_new(const char *name)
{
  struct Group *g;
  if (group_find(name))
    eprintf("manifest: duplicate group '%s'\n", name);
  if (rule_find(name))
    eprintf("manifest: name '%s' already a rule\n", name);
  GROW(groups, ngroups, groupscap, 16);
  g = &groups[ngroups++];
  memset(g, 0, sizeof *g);
  strlcpy(g->name, name, sizeof g->name);
  return g;
}

struct Meta *
meta_find(const char *name)
{
  int i;
  for (i = 0; i < nmetas; i++)
    if (strcmp(metas[i].name, name) == 0)
      return &metas[i];
  return NULL;
}

struct Meta *
meta_new(const char *name)
{
  struct Meta *m;
  if (meta_find(name))
    eprintf("manifest: duplicate meta '%s'\n", name);
  if (rule_find(name))
    eprintf("manifest: name '%s' already a rule\n", name);
  if (group_find(name))
    eprintf("manifest: name '%s' already a group\n", name);
  GROW(metas, nmetas, metascap, 8);
  m = &metas[nmetas++];
  memset(m, 0, sizeof *m);
  strlcpy(m->name, name, sizeof m->name);
  return m;
}

struct Template *
template_find(const char *name)
{
  int i;
  for (i = 0; i < ntemplates; i++)
    if (strcmp(templates[i].name, name) == 0)
      return &templates[i];
  return NULL;
}

struct Template *
template_new(const char *name)
{
  struct Template *t;
  if (template_find(name))
    eprintf("manifest: duplicate template '%s'\n", name);
  GROW(templates, ntemplates, templatescap, 8);
  t = &templates[ntemplates++];
  memset(t, 0, sizeof *t);
  strlcpy(t->name, name, sizeof t->name);
  return t;
}

int
inst_new(int rule_idx)
{
  struct Inst *in;
  struct Rule *r = &rules[rule_idx];

  GROW(insts, ninsts, instscap, 32);
  in = &insts[ninsts];
  memset(in, 0, sizeof *in);
  in->rule_idx = rule_idx;

  if (r->n_inst >= r->cap_inst) {
    r->cap_inst = r->cap_inst ? r->cap_inst * 2 : 4;
    r->inst_idx = erealloc(r->inst_idx, (size_t)r->cap_inst * sizeof *r->inst_idx);
  }
  r->inst_idx[r->n_inst++] = ninsts;
  return ninsts++;
}
