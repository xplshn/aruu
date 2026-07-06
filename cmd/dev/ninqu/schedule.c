#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>

/* kahn topo sort, one ready level at a time so each level
 * runs in parallel. failures are reported but do not stop the
 * batch, so one broken file does not block the rest */
void
schedule_and_run(void)
{
  int  *indeg      = ecalloc((size_t)ninsts, sizeof *indeg);
  int **dependents = emalloc((size_t)ninsts * sizeof *dependents);
  int  *ndep       = ecalloc((size_t)ninsts, sizeof *ndep);
  int  *capdep     = ecalloc((size_t)ninsts, sizeof *capdep);
  int  *level      = emalloc((size_t)ninsts * sizeof *level);
  int   levn = 0, done_count = 0;
  int   i, j;

  for (i = 0; i < ninsts; i++) {
    indeg[i]      = insts[i].n_dep;
    dependents[i] = NULL;
  }
  for (i = 0; i < ninsts; i++) {
    for (j = 0; j < insts[i].n_dep; j++) {
      int d = insts[i].dep_inst[j];
      if (ndep[d] >= capdep[d]) {
        capdep[d]     = capdep[d] ? capdep[d] * 2 : 4;
        dependents[d] = erealloc(dependents[d], (size_t)capdep[d] * sizeof *dependents[d]);
      }
      dependents[d][ndep[d]++] = i;
    }
  }
  for (i = 0; i < ninsts; i++)
    if (indeg[i] == 0)
      level[levn++] = i;

  while (levn > 0) {
    int *next_level = emalloc((size_t)ninsts * sizeof *next_level);
    int  nextn      = 0;

    if (summary_mode) {
      for (i = 0; i < levn; i++) {
        struct Inst *inst = &insts[level[i]];
        int          will = inst_stale(inst);
        int          d;
        for (d = 0; d < inst->n_dep && !will; d++)
          if (insts[inst->dep_inst[d]].will_build)
            will = 1;
        inst->will_build = will;
        printf(
            "%-4s %-12s %s\n",
            will ? "BUILD" : "OK",
            rules[inst->rule_idx].name,
            inst->out[0] ? inst->out : "(phony)"
        );
        if (rules[inst->rule_idx].description[0])
          printf("       # %s\n", rules[inst->rule_idx].description);
        if (will) {
          char *cmd = sl_join(&inst->cmd.argv);
          printf("       cmd: %s\n", cmd);
          free(cmd);
        }
      }
    } else {
      run_batch(level, levn);
    }

    done_count += levn;
    for (i = 0; i < levn; i++) {
      int u = level[i];
      for (j = 0; j < ndep[u]; j++) {
        int v = dependents[u][j];
        if (--indeg[v] == 0)
          next_level[nextn++] = v;
      }
    }
    free(level);
    level = next_level;
    levn  = nextn;
  }

  if (done_count < ninsts)
    eprintf("manifest: dependency cycle among %d instances\n", ninsts - done_count);

  free(level);
  free(indeg);
  for (i = 0; i < ninsts; i++)
    free(dependents[i]);
  free(dependents);
  free(ndep);
  free(capdep);
}
